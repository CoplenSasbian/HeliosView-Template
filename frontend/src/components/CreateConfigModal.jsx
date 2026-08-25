// CreateConfigModal.jsx — modal for creating a new config: a single name
// input (Enter submits). Owns its input state; the name is reported to the
// parent via onSubmit(name) on confirm.

import { useEffect, useState } from 'react'
import { Modal, Button, Field, Input } from './ui'

export default function CreateConfigModal({ open, busy, onClose, onSubmit }) {
  const [name, setName] = useState('')

  // Reset the input every time the modal opens.
  useEffect(() => {
    if (open) setName('')
  }, [open])

  const submit = () => {
    const trimmed = name.trim()
    if (trimmed) onSubmit(trimmed)
  }

  return (
    <Modal
      open={open}
      title="新建配置"
      onClose={onClose}
      actions={
        <>
          <Button onClick={onClose}>取消</Button>
          <Button variant="primary" disabled={busy || !name.trim()} onClick={submit}>
            {busy ? '创建中…' : '创建'}
          </Button>
        </>
      }
    >
      <Field label="配置名称">
        <Input
          autoFocus
          placeholder="例如：办公、游戏"
          value={name}
          onChange={(e) => setName(e.target.value)}
          onKeyDown={(e) => { if (e.key === 'Enter') submit() }}
          disabled={busy}
        />
      </Field>
    </Modal>
  )
}
