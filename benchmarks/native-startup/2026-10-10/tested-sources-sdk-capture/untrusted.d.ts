/**
 * A value received before SuiteCut has checked its runtime shape.
 *
 * Keep this type at process, JSON, and worker boundaries. Pass the value through
 * a schema or type guard before using it as a domain value.
 */
export type UntrustedInput = object | string | number | boolean | bigint | symbol | null | undefined;
/** Converts a rejected or thrown value into an Error without object stringification. */
export declare function toError(input: UntrustedInput): Error;
//# sourceMappingURL=untrusted.d.ts.map