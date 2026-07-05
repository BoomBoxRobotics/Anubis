"""Fusion 360 wrapper for the X5 gimbal model generator.

Fusion's script browser expects:
fusion360_x5_gimbal_model/
  fusion360_x5_gimbal_model.py
  fusion360_x5_gimbal_model.manifest

The actual model generator lives one folder up so it can also be edited and
reviewed directly from the Daughterboard project root.
"""

from __future__ import annotations

import importlib.util
import os
import traceback

import adsk.core


def _load_root_script():
    script_dir = os.path.dirname(os.path.realpath(__file__))
    project_dir = os.path.dirname(script_dir)
    root_script = os.path.join(project_dir, "fusion360_x5_gimbal_model.py")
    spec = importlib.util.spec_from_file_location("daughterboard_x5_gimbal_model_root", root_script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        module = _load_root_script()
        module.run(context)
    except Exception:
        if ui:
            ui.messageBox("X5 gimbal wrapper failed:\n{}".format(traceback.format_exc()))


def stop(context):
    pass
