// Typings for the native <-> JS bridge that HeliosView injects into every page:
//   const info = await window.helios.call('appInfo', {});
// See the template README for the full API.
declare global {
    interface Window {
        helios: {
            call(name: string, ...args: unknown[]): Promise<unknown>;
        };
    }
}
export {};
