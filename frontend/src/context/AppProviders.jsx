// AppProviders.jsx — unified provider composer. Flattens the provider tree
// so App.jsx doesn't suffer from pyramid-of-doom nesting.

import React from 'react'
import { AppearanceProvider } from './AppearanceContext.jsx'
import { LoggerProvider } from './LoggerContext.jsx'
import { ConfigProvider } from './ConfigContext.jsx'
import { PluginProvider } from './PluginContext.jsx'
import { SettingsProvider } from './SettingsContext.jsx'
import { ProcessProvider } from './ProcessContext.jsx'

const PROVIDERS = [
  AppearanceProvider,
  LoggerProvider,
  ConfigProvider,
  PluginProvider,
  SettingsProvider,
  ProcessProvider,
]

export function AppProviders({ children }) {
  return PROVIDERS.reduceRight(
    (acc, Provider) => <Provider>{acc}</Provider>,
    children,
  )
}
