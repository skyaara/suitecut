import { type ChildProcess } from 'node:child_process';
export interface SuiteCutProcessResult {
    executable: string;
    args: string[];
    exitCode: number | null;
    signal: NodeJS.Signals | null;
    stdout: string;
    stderr: string;
}
export interface SuiteCutProcessOptions {
    cwd?: string;
    signal?: AbortSignal;
    timeoutMs?: number;
    maxBufferBytes?: number;
    forceKillAfterMs?: number;
}
/** Waits for graceful exit, then escalates from SIGTERM to SIGKILL. */
export declare function waitForProcessExit(child: ChildProcess, completion: Promise<void>, options?: {
    gracefulTimeoutMs?: number;
    forceKillAfterMs?: number;
}): Promise<void>;
export declare function runProcess(executable: string, args: readonly string[], options?: SuiteCutProcessOptions): Promise<SuiteCutProcessResult>;
export declare function resolveFfmpeg(explicitPath?: string): Promise<string>;
export declare function resolveFfprobe(ffmpegPath?: string): Promise<string>;
//# sourceMappingURL=process.d.ts.map