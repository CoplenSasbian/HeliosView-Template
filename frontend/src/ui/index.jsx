// ui/index.jsx — single entry point for all UI primitives.
// Import from here instead of individual files:
//   import { Button, Card, toast, ToastContainer } from '../ui'

// ── Core components ──────────────────────────────────────────
export {
  Button,
  IconButton,
  Toggle,
  Segmented,
  Card,
  Field,
  Input,
  InputNumber,
  ColorPicker,
  Select,
  Range,
  Notice,
  Modal,
  Chip,
  Pill,
  SettingRow,
  ValueBadge,
  Collapse,
  DataTable,
} from './ui.jsx'

// ── Tooltip ──────────────────────────────────────────────────
export { default as Tooltip } from './Tooltip.jsx'

// ── Toast (imperative API + container) ───────────────────────
export { toast, default as ToastContainer } from './toast.jsx'

// ── Icons ────────────────────────────────────────────────────
export {
  IconHome,
  IconSettings,
  IconPlugin,
  IconMonitor,
  IconGauge,
  IconPlus,
  IconTrash,
  IconFolder,
  IconInfo,
  IconLogo,
  IconSun,
  IconMoon,
  IconAuto,
  IconGrid,
  IconList,
  IconSearch,
  IconRefresh,
  IconSave,
  IconUndo,
} from './icons.jsx'

// ── Helios window chrome ──────────────────────────────────────
export { HeliosTitleBar, HeliosWindowControls } from './helios.jsx'
