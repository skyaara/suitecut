import { type SuiteCutAudioPlugin } from './audio-plugin.js';
import { type SuiteCutAudioPluginReference } from './schemas.js';
/** Parses plugin references, rejects collisions, and resolves local module paths. */
export declare function resolveAudioPluginReferences(references: readonly SuiteCutAudioPluginReference[], baseDirectory?: string): ReadonlyMap<string, SuiteCutAudioPluginReference>;
/** Loads and checks one external plugin module inside the narration worker. */
export declare function loadAudioPlugin(reference: SuiteCutAudioPluginReference): Promise<SuiteCutAudioPlugin>;
//# sourceMappingURL=audio-plugin-loader.d.ts.map