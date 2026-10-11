import { type UntrustedInput } from './untrusted.js';
/** Replaces a file through a temporary file in the same directory. */
export declare function writeFileAtomic(path: string, contents: string | Uint8Array): Promise<void>;
/** Replaces a UTF-8 text file atomically. */
export declare function writeTextFileAtomic(path: string, contents: string): Promise<void>;
/** Serializes JSON with stable indentation and a trailing newline. */
export declare function formattedJson(value: UntrustedInput): string;
//# sourceMappingURL=atomic-file.d.ts.map