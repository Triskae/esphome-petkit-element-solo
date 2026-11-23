"""Header-only feed controller for the PETKIT Fresh Element Solo.

Adding `feeder_motor:` to a config pulls `feeder::motor()` into the build, for the
lambdas in feeder.yaml. All the logic is in feeder_motor.h.
"""

import esphome.codegen as cg
import esphome.config_validation as cv

CODEOWNERS = ["@Triskae"]

CONFIG_SCHEMA = cv.Schema({})


async def to_code(config):
    cg.add_global(cg.RawStatement('#include "esphome/components/feeder_motor/feeder_motor.h"'))
