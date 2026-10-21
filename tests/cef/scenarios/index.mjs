// SPDX-License-Identifier: MIT
import { application } from './application.mjs'
import { frames } from './frames.mjs'
import { network } from './network.mjs'
import { races } from './races.mjs'
import { artifacts } from './artifacts.mjs'
import { capture } from './capture.mjs'
import { recording } from './recording.mjs'

export const scenarios = [
  ...application,
  ...frames,
  ...network,
  ...races,
  ...artifacts,
  ...capture,
  ...recording,
]
