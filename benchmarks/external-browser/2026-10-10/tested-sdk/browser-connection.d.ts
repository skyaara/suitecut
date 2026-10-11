import { type Browser } from 'playwright';
import * as z from 'zod';
declare const ConnectionOptions: z.ZodUnion<readonly [z.ZodObject<{
    protocol: z.ZodDefault<z.ZodLiteral<"cdp">>;
    browserName: z.ZodDefault<z.ZodLiteral<"chromium">>;
    noDefaults: z.ZodDefault<z.ZodBoolean>;
    endpoint: z.ZodURL;
    headers: z.ZodOptional<z.ZodRecord<z.ZodString, z.ZodString>>;
    timeoutMs: z.ZodDefault<z.ZodNumber>;
    slowMoMs: z.ZodDefault<z.ZodNumber>;
}, z.core.$strict>, z.ZodObject<{
    protocol: z.ZodLiteral<"playwright">;
    browserName: z.ZodDefault<z.ZodEnum<{
        chromium: "chromium";
        firefox: "firefox";
        webkit: "webkit";
    }>>;
    exposeNetwork: z.ZodOptional<z.ZodString>;
    endpoint: z.ZodURL;
    headers: z.ZodOptional<z.ZodRecord<z.ZodString, z.ZodString>>;
    timeoutMs: z.ZodDefault<z.ZodNumber>;
    slowMoMs: z.ZodDefault<z.ZodNumber>;
}, z.core.$strict>]>;
/** Settings for attaching to an independently managed browser endpoint. */
export type BrowserConnectionOptions = z.input<typeof ConnectionOptions>;
/** Connects ordinary Playwright automation to a caller-owned external browser. */
export declare function connectBrowser(options: BrowserConnectionOptions): Promise<Browser>;
export {};
//# sourceMappingURL=browser-connection.d.ts.map