import * as z from 'zod';
export declare const NATIVE_PROTOCOL_VERSION = 1;
export declare const MAX_NATIVE_PAYLOAD: number;
export declare const NativePacketSchema: z.ZodDiscriminatedUnion<[z.ZodObject<{
    type: z.ZodLiteral<"ready">;
    timestampMs: z.ZodNumber;
    cefVersion: z.ZodString;
    frameSubscriptions: z.ZodOptional<z.ZodBoolean>;
    audioSubscriptions: z.ZodOptional<z.ZodBoolean>;
    nativeMedia: z.ZodOptional<z.ZodBoolean>;
    version: z.ZodLiteral<1>;
    bytes: z.ZodNumber;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"frame">;
    pixelFormat: z.ZodDefault<z.ZodEnum<{
        bgra: "bgra";
        i420: "i420";
    }>>;
    timestampMs: z.ZodNumber;
    width: z.ZodNumber;
    height: z.ZodNumber;
    dropped: z.ZodNumber;
    version: z.ZodLiteral<1>;
    bytes: z.ZodNumber;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"audio">;
    timestampMs: z.ZodNumber;
    frames: z.ZodNumber;
    version: z.ZodLiteral<1>;
    bytes: z.ZodNumber;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"response">;
    id: z.ZodNumber;
    ok: z.ZodBoolean;
    error: z.ZodOptional<z.ZodString>;
    version: z.ZodLiteral<1>;
    bytes: z.ZodNumber;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"event">;
    topic: z.ZodLiteral<"cdp">;
    version: z.ZodLiteral<1>;
    bytes: z.ZodNumber;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"error">;
    error: z.ZodString;
    version: z.ZodLiteral<1>;
    bytes: z.ZodNumber;
}, z.core.$strict>], "type">;
export type NativePacket = z.infer<typeof NativePacketSchema>;
/** Incrementally decodes length-prefixed packets with bounded allocation and no quadratic concatenation. */
export declare class NativePacketDecoder {
    private readonly receive;
    private target;
    private offset;
    private phase;
    private header;
    constructor(receive: (header: NativePacket, payload: Buffer) => void);
    push(chunk: Buffer): void;
    end(): void;
    private reset;
}
//# sourceMappingURL=native-protocol.d.ts.map