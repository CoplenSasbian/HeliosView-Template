// Typings for the native <-> JS bridge that HeliosView injects into every page:
//   const state = await window.helios.call('plugins_getState');
//   await window.helios.call('plugins_activate', { config: 'my-config' });
//   await window.helios.call('plugins_setParams', { params: [{ plugin, name, value }] });
//
// Available native functions (registered in MainWindow::setupBridge):
//   - plugins_getState  () -> {plugins, configs, activeConfig,
//                              info: {plugin: [{name, desc, type, min, max, step, defaultValue}]},
//                              params: {plugin: {name: value}}}  // params only when a config is active
//   - plugins_activate  ({config: string})              -> {ok: true}
//   - plugins_setParams ({params: [{plugin, name, value}]}) -> {ok: true}
//   - plugins_pickPath  ({type: 'file'|'folder', title?}) -> {ok: bool, path: string}
// Note: bindJson names must be C identifiers (no dots).
declare global {
    interface Window {
        helios: {
            call(
                name:
                    | 'plugins_getState'
                    | 'plugins_activate'
                    | 'plugins_setParams'
                    | 'plugins_pickPath',
                ...args: unknown[]
            ): Promise<unknown>;
        };
    }
}
export {};
