import fs from "node:fs/promises";
import path from "node:path";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const root = process.cwd();
const outDir = path.join(root, "fabrication", "jlcpcb_2026-07-10_r13");
const assemblyDir = path.join(outDir, "assembly");
const reportsDir = path.join(outDir, "reports");

const kicadBomPath = path.join(assemblyDir, "Daughterboard_kicad_bom.csv");
const kicadPosPath = path.join(assemblyDir, "Daughterboard_kicad_pos.csv");
const oldAuditPath = path.join(root, "Daughterboard_current_jlc_part_audit.csv");

function parseCsv(text) {
  const rows = [];
  let row = [];
  let field = "";
  let quoted = false;
  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    const next = text[i + 1];
    if (quoted) {
      if (ch === '"' && next === '"') {
        field += '"';
        i += 1;
      } else if (ch === '"') {
        quoted = false;
      } else {
        field += ch;
      }
    } else if (ch === '"') {
      quoted = true;
    } else if (ch === ",") {
      row.push(field);
      field = "";
    } else if (ch === "\n") {
      row.push(field);
      rows.push(row);
      row = [];
      field = "";
    } else if (ch !== "\r") {
      field += ch;
    }
  }
  if (field.length || row.length) {
    row.push(field);
    rows.push(row);
  }
  const [headers, ...body] = rows.filter((r) => r.some((v) => v !== ""));
  return body.map((r) => Object.fromEntries(headers.map((h, i) => [h, r[i] ?? ""])));
}

function csvEscape(value) {
  const s = String(value ?? "");
  if (/[",\n\r]/.test(s)) return `"${s.replaceAll('"', '""')}"`;
  return s;
}

function toCsv(headers, rows) {
  return [
    headers.map(csvEscape).join(","),
    ...rows.map((row) => headers.map((h) => csvEscape(row[h])).join(",")),
  ].join("\n") + "\n";
}

function refSortKey(ref) {
  const m = /^([A-Z]+)(\d+)$/.exec(ref);
  if (!m) return [ref, 0];
  return [m[1], Number(m[2])];
}

function compareRef(a, b) {
  const ak = refSortKey(a);
  const bk = refSortKey(b);
  if (ak[0] !== bk[0]) return ak[0] < bk[0] ? -1 : 1;
  return ak[1] - bk[1];
}

function expandRefs(refs) {
  const result = [];
  for (const chunk of refs.split(",")) {
    const part = chunk.trim();
    const range = /^([A-Z]+)(\d+)-([A-Z]+)?(\d+)$/.exec(part);
    if (range) {
      const prefix = range[1];
      const endPrefix = range[3] || prefix;
      if (prefix === endPrefix) {
        for (let n = Number(range[2]); n <= Number(range[4]); n += 1) result.push(`${prefix}${n}`);
        continue;
      }
    }
    if (part) result.push(part);
  }
  return result;
}

function loadAuditByRef(rows) {
  const byRef = new Map();
  for (const row of rows) {
    for (const ref of expandRefs(row.Designator || "")) {
      byRef.set(ref, row);
    }
  }
  return byRef;
}

function isAssemblyCandidate(row, audit) {
  const ref = row.Ref || row.Designator;
  const fp = row.Footprint || row.Package || "";
  const value = row.Value || row.Val || row.Comment || "";
  const status = audit?.Status || "";
  if (!fp) return false;
  if (/^(BT|TP)/.test(ref)) return false;
  if (/^J(3[0-9]|9[0-9]+)/.test(ref)) return false;
  if (/TestPoint|Pad_/.test(fp)) return false;
  if (/Pololu_2808/.test(fp)) return false;
  if (/DNP/i.test(value) || /User-installed|Board feature|No assembly/i.test(status)) return false;
  return true;
}

function normalizeValue(ref, value) {
  if (ref === "Q1") return "CSD16412Q5A charge FET";
  if (ref === "Q2") return "CSD16412Q5A discharge FET";
  if (ref === "R49" || ref === "R50") return "10M";
  return value;
}

function overrideAudit(ref, value, footprint, audit) {
  if (ref === "Q1" || ref === "Q2") {
    return {
      Status: "Needs JLC selection",
      LCSC: "",
      MPN: "CSD16412Q5A",
      JLCType: "",
      Note: "R13 high-side protection FET; choose JLC-compatible part/footprint or accept outside-sourced assembly.",
    };
  }
  if (ref === "R49" || ref === "R50") {
    return {
      Status: "Needs JLC selection",
      LCSC: "",
      MPN: "10M 0603 resistor",
      JLCType: "",
      Note: "R13 gate-source bleed resistor; select a JLC 0603 10M part before assembly upload.",
    };
  }
  if (ref === "C32" || ref === "C33") {
    return {
      Status: "Ready",
      LCSC: "C14663",
      MPN: "CC0603KRX7R9BB104",
      JLCType: "Basic",
      Note: "Same 100nF X7R 0603 JLC part used elsewhere; voltage rating is suitable for the 25V note.",
    };
  }
  return audit || {
    Status: "Needs audit",
    LCSC: "",
    MPN: "",
    JLCType: "",
    Note: "No carried-forward JLC part match found for this R13 item.",
  };
}

await fs.mkdir(assemblyDir, { recursive: true });
await fs.mkdir(reportsDir, { recursive: true });

const kicadBom = parseCsv(await fs.readFile(kicadBomPath, "utf8"));
const kicadPos = parseCsv(await fs.readFile(kicadPosPath, "utf8"));
const auditByRef = loadAuditByRef(parseCsv(await fs.readFile(oldAuditPath, "utf8")));
const bomByRef = new Map();

for (const row of kicadBom) {
  for (const ref of expandRefs(row.Refs || row.Reference || "")) {
    bomByRef.set(ref, {
      ref,
      value: normalizeValue(ref, row.Value || ""),
      footprint: row.Footprint || "",
      qty: 1,
    });
  }
}

const posByRef = new Map(kicadPos.map((row) => [row.Ref, row]));
const assemblyRefs = [...bomByRef.keys()]
  .filter((ref) => isAssemblyCandidate({
    Ref: ref,
    Value: bomByRef.get(ref).value,
    Footprint: bomByRef.get(ref).footprint,
  }, auditByRef.get(ref)))
  .sort(compareRef);

const bomRows = [];
const auditRows = [];
const cplRows = [];
const missingRows = [];

for (const ref of assemblyRefs) {
  const part = bomByRef.get(ref);
  const audit = overrideAudit(ref, part.value, part.footprint, auditByRef.get(ref));
  bomRows.push({
    Comment: part.value,
    Designator: ref,
    Footprint: part.footprint,
    "JLCPCB Part#(optional)": audit.LCSC || "",
  });
  auditRows.push({
    Status: audit.Status,
    Designator: ref,
    Comment: part.value,
    Footprint: part.footprint,
    Quantity: "1",
    LCSC: audit.LCSC || "",
    MPN: audit.MPN || "",
    JLCType: audit.JLCType || "",
    Note: audit.Note || "",
  });
  if (!audit.LCSC) {
    missingRows.push(auditRows.at(-1));
  }
  const pos = posByRef.get(ref);
  if (pos) {
    cplRows.push({
      Designator: ref,
      "Mid X": Number(pos.PosX).toFixed(4),
      "Mid Y": Math.abs(Number(pos.PosY)).toFixed(4),
      Layer: String(pos.Side).toLowerCase() === "bottom" ? "Bottom" : "Top",
      Rotation: Number(pos.Rot).toFixed(2),
    });
  }
}

await fs.writeFile(
  path.join(assemblyDir, "Daughterboard_jlcpcb_bom_template_filled.csv"),
  toCsv(["Comment", "Designator", "Footprint", "JLCPCB Part#(optional)"], bomRows),
);
await fs.writeFile(
  path.join(assemblyDir, "Daughterboard_jlcpcb_cpl.csv"),
  toCsv(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"], cplRows),
);
await fs.writeFile(
  path.join(assemblyDir, "Daughterboard_jlcpcb_cpl_all.csv"),
  toCsv(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"], cplRows),
);
await fs.writeFile(
  path.join(outDir, "Daughterboard_r13_jlc_part_audit.csv"),
  toCsv(["Status", "Designator", "Comment", "Footprint", "Quantity", "LCSC", "MPN", "JLCType", "Note"], auditRows),
);
await fs.writeFile(
  path.join(reportsDir, "Daughterboard_r13_missing_jlc_parts.csv"),
  toCsv(["Status", "Designator", "Comment", "Footprint", "Quantity", "LCSC", "MPN", "JLCType", "Note"], missingRows),
);

const workbook = await Workbook.fromCSV(
  toCsv(["Comment", "Designator", "Footprint", "JLCPCB Part#(optional)"], bomRows),
  { sheetName: "JLCPCB BOM" },
);
const sheet = workbook.worksheets.getItem("JLCPCB BOM");
sheet.getRange("A1:D1").format = {
  fill: "#1F4E78",
  font: { bold: true, color: "#FFFFFF" },
};
sheet.getRange("A:D").format.wrapText = false;
sheet.getRange("A:D").format.autofitColumns();
sheet.freezePanes.freezeRows(1);

const preview = await workbook.render({ sheetName: "JLCPCB BOM", range: "A1:D30", scale: 1, format: "png" });
await fs.writeFile(
  path.join(assemblyDir, "Daughterboard_jlcpcb_bom_template_filled_preview.png"),
  new Uint8Array(await preview.arrayBuffer()),
);
const xlsx = await SpreadsheetFile.exportXlsx(workbook);
await xlsx.save(path.join(assemblyDir, "Daughterboard_jlcpcb_bom_template_filled.xlsx"));

console.log(`Wrote ${bomRows.length} assembly BOM rows.`);
console.log(`Wrote ${cplRows.length} CPL rows.`);
console.log(`Missing/needs-selection JLC rows: ${missingRows.length}.`);
