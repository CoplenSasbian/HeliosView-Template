// ConfirmModal.jsx — generic confirmation dialog for destructive or
// irreversible actions. Reused anywhere a "are you sure?" step is needed.

import { Modal, Button } from './ui'

export default function ConfirmModal({
  open,
  title = '确认操作',
  message,
  confirmText = '确定',
  busy = false,
  danger = false,
  onClose,
  onConfirm,
}) {
  return (
    <Modal
      open={open}
      title={title}
      onClose={onClose}
      actions={
        <>
          <Button variant="ghost" onClick={onClose} disabled={busy}>取消</Button>
          <Button
            variant={danger ? 'danger' : 'primary'}
            disabled={busy}
            onClick={onConfirm}
          >
            {busy ? '处理中…' : confirmText}
          </Button>
        </>
      }
    >
     <p className="field__desc">{message}</p>
    </Modal>
  )
}
