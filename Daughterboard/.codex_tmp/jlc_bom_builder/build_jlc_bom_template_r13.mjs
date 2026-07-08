import fs from "node:fs/promises";
import path from "node:path";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const root = path.resolve("../..");
const assemblyDir = path.join(root, "fabrication", "jlcpcb_2026-07-06_r13", "assembly");
const inputCsv = path.join(assemblyDir, "Daughterboard_jlcpcb_bom_jlc_ready_draft.csv");
const cplCsv = path.join(assemblyDir, "Daughterboard_jlcpcb_cpl.csv");
const outputXlsx = path.join(assemblyDir, "Daughterboard_jlcpcb_bom_template_filled.xlsx");
const outputCsv = path.join(assemblyDir, "Daughterboard_jlcpcb_bom_template_filled.csv");
const previewPng = path.join(assemblyDir, "Daughterboard_jlcpcb_bom_template_filled_preview.png");

function parseCsv(text) {
  const rows = [];
  let row = [];
  let cell = "";
  let inQuotes = false;

  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    const next = text[i + 1];
    if (inQuotes) {
      if (ch === '"' && next === '"') {
        cell += '"';
        i += 1;
      } else if (ch === '"') {
        inQuotes = false;
      } else {
        cell += ch;
      }
    } else if (ch === '"') {
      inQuotes = true;
    } else if (ch === ",") {
      row.push(cell);
      cell = "";
    } else if (ch === "\n") {
      row.push(cell);
      rows.push(row);
      row = [];
      cell = "";
    } else if (ch !== "\r") {
      cell += ch;
    }
  }
  if (cell.length || row.length) {
    row.push(cell);
    rows.push(row);
  }
  return rows;
}

const csvText = await fs.readFile(inputCsv, "utf8");
const parsed = parseCsv(csvText);
const sourceHeaders = parsed[0];
const records = parsed.slice(1).filter((r) => r.some((v) => v.trim() !== ""));
const idx = Object.fromEntries(sourceHeaders.map((h, i) => [h, i]));

const cplText = await fs.readFile(cplCsv, "utf8");
const cplParsed = parseCsv(cplText);
const cplHeaders = cplParsed[0];
const cplRefIndex = cplHeaders.indexOf("Designator");
const cplRefs = new Set(
  cplParsed.slice(1)
    .map((r) => r[cplRefIndex])
    .filter(Boolean),
);

function expandRefs(refText) {
  const expanded = [];
  for (const rawToken of refText.split(",")) {
    const token = rawToken.trim();
    const match = token.match(/^([A-Za-z]+)(\d+)-([A-Za-z]*)(\d+)$/);
    if (match) {
      const prefix = match[1];
      const start = Number(match[2]);
      const endPrefix = match[3] || prefix;
      const end = Number(match[4]);
      if (endPrefix === prefix && end >= start) {
        for (let n = start; n <= end; n += 1) expanded.push(`${prefix}${n}`);
      } else {
        expanded.push(token);
      }
    } else if (token) {
      expanded.push(token);
    }
  }
  return expanded;
}

const headers = ["Comment", "Designator", "Footprint", "JLCPCB Part#(optional)"];
const skippedRefs = [];
const rows = [];
for (const r of records) {
  const refs = expandRefs(r[idx.Designator] ?? "");
  for (const ref of refs) {
    if (!cplRefs.has(ref)) {
      skippedRefs.push(ref);
      continue;
    }
    rows.push([
      r[idx.Comment] ?? "",
      ref,
      r[idx.Footprint] ?? "",
      r[idx["LCSC Part #"]] ?? "",
    ]);
  }
}

const workbook = Workbook.create();
const sheet = workbook.worksheets.add("JLCPCB_BOM Template");
sheet.showGridLines = true;

sheet.getRange("A1:D1").values = [headers];
if (rows.length) {
  sheet.getRangeByIndexes(1, 0, rows.length, headers.length).values = rows;
}

const usedRange = sheet.getRangeByIndexes(0, 0, rows.length + 1, headers.length);
usedRange.format = {
  font: { name: "Calibri", size: 11 },
  borders: { preset: "all", style: "thin", color: "#D9D9D9" },
};

const headerRange = sheet.getRange("A1:D1");
headerRange.format = {
  fill: "#D9EAD3",
  font: { name: "Calibri", size: 11, bold: true, color: "#000000" },
  borders: { preset: "all", style: "thin", color: "#A6A6A6" },
};

sheet.getRange("A:A").format.columnWidth = 34;
sheet.getRange("B:B").format.columnWidth = 30;
sheet.getRange("C:C").format.columnWidth = 52;
sheet.getRange("D:D").format.columnWidth = 22;
sheet.getRange(`A1:D${rows.length + 1}`).format.wrapText = false;
sheet.freezePanes.freezeRows(1);

const inspect = await workbook.inspect({
  kind: "table",
  range: `JLCPCB_BOM Template!A1:D${Math.min(rows.length + 1, 12)}`,
  include: "values",
  tableMaxRows: 12,
  tableMaxCols: 4,
  maxChars: 4000,
});
console.log(inspect.ndjson);

const errors = await workbook.inspect({
  kind: "match",
  searchTerm: "#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A",
  options: { useRegex: true, maxResults: 50 },
  maxChars: 1000,
});
console.log(errors.ndjson);

const preview = await workbook.render({
  sheetName: "JLCPCB_BOM Template",
  range: `A1:D${Math.min(rows.length + 1, 30)}`,
  scale: 1,
  format: "png",
});
await fs.writeFile(previewPng, new Uint8Array(await preview.arrayBuffer()));

const output = await SpreadsheetFile.exportXlsx(workbook);
await output.save(outputXlsx);

function csvEscape(value) {
  const s = String(value ?? "");
  return `"${s.replaceAll('"', '""')}"`;
}
await fs.writeFile(
  outputCsv,
  [headers, ...rows].map((row) => row.map(csvEscape).join(",")).join("\r\n") + "\r\n",
  "utf8",
);

console.log(JSON.stringify({ outputXlsx, outputCsv, previewPng, rows: rows.length, skippedRefs }));
