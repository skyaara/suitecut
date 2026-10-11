import { rename } from 'node:fs/promises';
/** Publish a verified directory atomically despite short-lived Windows scanner locks. */
export declare function publishMediaDirectory(source: string, destination: string, options?: {
    platform?: NodeJS.Platform;
    renameDirectory?: typeof rename;
    wait?: (milliseconds: number) => Promise<void>;
}): Promise<void>;
/** Streams an archive to disk and rejects any bytes that differ from the pinned build. */
export declare function downloadMediaArchive(url: string, sha256: string, destination: string): Promise<void>;
/** Extract only executables and notices to flat, controlled paths; reject links and duplicate names. */
export declare function extractMediaArchive(archive: string, directory: string): Promise<void>;
/** Checks the baseline codecs and protocols used by SuiteCut, without contacting the network. */
export declare function verifyMediaTools(paths: {
    ffmpeg: string;
    ffprobe: string;
}, managed?: boolean): Promise<void>;
/** Explicit setup only. A validated pair is published atomically; failed downloads leave no cache entry. */
export declare function installMediaTools(): Promise<{
    ffmpeg: string;
    ffprobe: string;
}>;
//# sourceMappingURL=media-tools.d.ts.map