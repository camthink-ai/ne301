import * as React from "preact/compat"
import { createPortal } from 'preact/compat'
import { useState, useRef, useEffect, useCallback, useMemo, useLayoutEffect } from 'preact/hooks'
import { cn } from "@/lib/utils"
import { Dialog, DialogContent } from './dialog'
import { useIsMobile } from '@/hooks/use-mobile'

interface TooltipContextValue {
  open: boolean
  setOpen: (open: boolean) => void
  dialogOpen: boolean
  setDialogOpen: (open: boolean) => void
  triggerRef: React.RefObject<HTMLElement>
  contentRef: React.RefObject<HTMLDivElement>
  side: 'top' | 'bottom' | 'left' | 'right'
  setSide: (side: 'top' | 'bottom' | 'left' | 'right') => void
  sideOffset: number
  isMobile: boolean
  mbEnhance: boolean
}

const TooltipContext = React.createContext<TooltipContextValue | null>(null)

function useTooltipContext() {
  const context = React.useContext(TooltipContext)
  if (!context) {
    throw new Error('Tooltip components must be used within a TooltipProvider')
  }
  return context
}

function Tooltip({
  delayDuration = 0,
  side: defaultSide = 'top',
  mbEnhance = false,
  children,
  ...props
}: React.ComponentProps<'div'> & { delayDuration?: number; side?: 'top' | 'bottom' | 'left' | 'right'; mbEnhance?: boolean }) {
  const [open, setOpen] = useState(false)
  const [dialogOpen, setDialogOpen] = useState(false)
  const [side, setSide] = useState<'top' | 'bottom' | 'left' | 'right'>(defaultSide)
  const [sideOffset] = useState(0)
  const triggerRef = useRef<HTMLElement>(null)
  const contentRef = useRef<HTMLDivElement>(null)
  const timeoutRef = useRef<NodeJS.Timeout>()
  const isMobile = useIsMobile()

  const handleOpenChange = useCallback((newOpen: boolean) => {
    if (delayDuration > 0 && newOpen) {
      timeoutRef.current = setTimeout(() => setOpen(true), delayDuration)
    } else {
      setOpen(newOpen)
    }
  }, [delayDuration])

  useEffect(() => () => {
    if (timeoutRef.current) clearTimeout(timeoutRef.current)
  }, [])

  const contextValue = useMemo<TooltipContextValue>(() => ({
    open,
    setOpen: handleOpenChange,
    dialogOpen,
    setDialogOpen,
    triggerRef,
    contentRef,
    side,
    setSide,
    sideOffset,
    isMobile,
    mbEnhance
  }), [open, handleOpenChange, dialogOpen, setDialogOpen, side, setSide, sideOffset, isMobile, mbEnhance])

  return (
    <TooltipContext.Provider value={contextValue}>
      {React.isValidElement(children) ? (React.cloneElement(children as React.ReactElement<any>, props)) : children}
    </TooltipContext.Provider>
  )
}

// Compatibility wrapper: keep export but no longer required. Can safely wrap or ignore.
function TooltipProvider({ children, ...props }: React.ComponentProps<'div'>) {
  return <div {...props}>{children}</div>
}

function TooltipTrigger({
  children,
  asChild = false,
  ...props
}: React.ComponentProps<'div'> & { asChild?: boolean }) {
  const { setOpen, setDialogOpen, isMobile, mbEnhance, triggerRef } = useTooltipContext()

  const handleMouseEnter = () => {
    if (!isMobile || !mbEnhance) {
      setOpen(true)
    }
  }
  const handleMouseLeave = () => {
    if (!isMobile || !mbEnhance) {
      setOpen(false)
    }
  }
  const handleFocus = () => {
    if (!isMobile || !mbEnhance) {
      setOpen(true)
    }
  }
  const handleBlur = () => {
    if (!isMobile || !mbEnhance) {
      setOpen(false)
    }
  }
  const handleClick = () => {
    if (isMobile && mbEnhance) {
      setDialogOpen(true)
    }
  }

  if (asChild && React.isValidElement(children)) {
    return React.cloneElement(children as React.ReactElement<any>, {
      ref: triggerRef,
      onMouseEnter: handleMouseEnter,
      onMouseLeave: handleMouseLeave,
      onFocus: handleFocus,
      onBlur: handleBlur,
      onClick: handleClick,
      ...props
    })
  }

  return (
    <div
      className="relative flex items-center justify-center"
      ref={triggerRef as React.Ref<HTMLDivElement>}
      onMouseEnter={handleMouseEnter}
      onMouseLeave={handleMouseLeave}
      onFocus={handleFocus}
      onBlur={handleBlur}
      onClick={handleClick}
      {...props}
    >
      {children}
    </div>
  )
}

function TooltipContent({
  className,
  sideOffset = 0,
  children,
  ...props
}: React.ComponentProps<'div'> & { sideOffset?: number }) {
  const { open, dialogOpen, setDialogOpen, triggerRef, contentRef, side, setSide, isMobile, mbEnhance } = useTooltipContext()
  const [position, setPosition] = useState({ x: 0, y: 0 })
  const [arrowColor, setArrowColor] = useState<string>('rgba(0,0,0,0.9)')

  // mobile display Dialog
  if (isMobile && mbEnhance && dialogOpen) {
    return (
      <Dialog open={dialogOpen} onOpenChange={setDialogOpen}>

        <DialogContent className="max-w-md" showCloseButton>
          <div className="text-sm pt-8">{children}</div>
        </DialogContent>
      </Dialog>
    )
  }

  // desktop display Tooltip
  if (!open) return null

  const updatePosition = useCallback(() => {
    // if (!triggerRef.current || !contentRef.current) return

    const triggerElement = triggerRef.current
    const contentElement = contentRef.current

    if (!(triggerElement instanceof HTMLElement) || !(contentElement instanceof HTMLElement)) {
      return
    }

    const triggerRect = triggerElement.getBoundingClientRect()
    const contentRect = contentElement.getBoundingClientRect()

    let x = 0
    let y = 0
    let newSide = side

    const spaceAbove = triggerRect.top
    const spaceBelow = window.innerHeight - triggerRect.bottom
    const spaceLeft = triggerRect.left
    const spaceRight = window.innerWidth - triggerRect.right

    // Prefer specified side, only auto-adjust when space is insufficient
    if (side === 'top' && spaceAbove >= contentRect.height + sideOffset) {
      newSide = 'top'
      x = triggerRect.left + (triggerRect.width / 2) - (contentRect.width / 2)
      y = triggerRect.top - contentRect.height - sideOffset - 10
    } else if (side === 'bottom' && spaceBelow >= contentRect.height + sideOffset) {
      newSide = 'bottom'
      x = triggerRect.left + (triggerRect.width / 2) - (contentRect.width / 2)
      y = triggerRect.bottom + sideOffset + 10
    } else if (side === 'left' && spaceLeft >= contentRect.width + sideOffset) {
      newSide = 'left'
      x = triggerRect.left - contentRect.width - sideOffset - 10
      y = triggerRect.top + (triggerRect.height / 2) - (contentRect.height / 2)
    } else if (side === 'right' && spaceRight >= contentRect.width + sideOffset) {
      newSide = 'right'
      x = triggerRect.right + sideOffset + 10
      y = triggerRect.top + (triggerRect.height / 2) - (contentRect.height / 2)
    } else if (spaceBelow >= contentRect.height + sideOffset && spaceBelow >= spaceAbove) {
      // If specified direction has insufficient space, automatically select best direction
      newSide = 'bottom'
      x = triggerRect.left + (triggerRect.width / 2) - (contentRect.width / 2)
      y = triggerRect.bottom + sideOffset + 10
    } else if (spaceAbove >= contentRect.height + sideOffset) {
      newSide = 'top'
      x = triggerRect.left + (triggerRect.width / 2) - (contentRect.width / 2)
      y = triggerRect.top - contentRect.height - sideOffset - 10
    } else if (spaceRight >= contentRect.width + sideOffset && spaceRight >= spaceLeft) {
      newSide = 'right'
      x = triggerRect.right + sideOffset + 10
      y = triggerRect.top + (triggerRect.height / 2) - (contentRect.height / 2)
    } else {
      newSide = 'left'
      x = triggerRect.left - contentRect.width - sideOffset - 10
      y = triggerRect.top + (triggerRect.height / 2) - (contentRect.height / 2)
    }

    x = Math.max(8, Math.min(x, window.innerWidth - contentRect.width - 8))
    y = Math.max(8, Math.min(y, window.innerHeight - contentRect.height - 8))

    setPosition({ x, y })

    if (newSide !== side) {
      setSide(newSide)
    }

    // Sync arrow color to Tooltip background color
    const bg = window.getComputedStyle(contentElement).backgroundColor
    if (bg && bg !== arrowColor) setArrowColor(bg)
  }, [triggerRef, contentRef, sideOffset, side, setSide, arrowColor])

  // Calculate once when first opened to avoid first frame flicker at (0,0)
  useLayoutEffect(() => {
    if (open) {
      updatePosition()
    }
  }, [open, updatePosition])

  useEffect(() => {
    if (open) {
      const timer = requestAnimationFrame(() => {
        updatePosition()
      })

      window.addEventListener('scroll', updatePosition)
      window.addEventListener('resize', updatePosition)

      return () => {
        cancelAnimationFrame(timer)
        window.removeEventListener('scroll', updatePosition)
        window.removeEventListener('resize', updatePosition)
      }
    }
    return undefined
  }, [open, updatePosition])

  const content = (
    <div
      ref={contentRef}
      className={cn(
        "fixed z-50 bg-background-primary text-background-primary-foreground animate-in fade-in-0 zoom-in-95 data-[state=closed]:animate-out data-[state=closed]:fade-out-0 data-[state=closed]:zoom-out-95 data-[side=bottom]:slide-in-from-top-2 data-[side=left]:slide-in-from-right-2 data-[side=right]:slide-in-from-left-2 data-[side=top]:slide-in-from-bottom-2 w-fit origin-(--radix-tooltip-content-transform-origin) rounded-md px-3 py-1.5 text-xs text-balance",
        className
      )}
      style={{
        left: position.x,
        top: position.y,
        transform: 'translate(0, 0)',
      }}
      role="tooltip"
      {...props}
    >
      {children}
      <div
        className="absolute w-0 h-0"
        style={{
          ...(side === 'top' && {
            top: '100%',
            left: '50%',
            transform: 'translateX(-50%) translateY(-2px)',
            borderLeft: '6px solid transparent',
            borderRight: '6px solid transparent',
            borderTop: `6px solid ${arrowColor}`,
          }),
          ...(side === 'bottom' && {
            bottom: '100%',
            left: '50%',
            transform: 'translateX(-50%) translateY(2px)',
            borderLeft: '6px solid transparent',
            borderRight: '6px solid transparent',
            borderBottom: `6px solid ${arrowColor}`,
          }),
          ...(side === 'left' && {
            left: '100%',
            top: '50%',
            transform: 'translateY(-50%) translateX(-2px)',
            borderTop: '6px solid transparent',
            borderBottom: '6px solid transparent',
            borderLeft: `6px solid ${arrowColor}`,
          }),
          ...(side === 'right' && {
            right: '100%',
            top: '50%',
            transform: 'translateY(-50%) translateX(2px)',
            borderTop: '6px solid transparent',
            borderBottom: '6px solid transparent',
            borderRight: `6px solid ${arrowColor}`,
          }),
        }}
      />
    </div>
  )

  // Render to body via portal to avoid being affected by ancestor transform
  return typeof document !== 'undefined' ? createPortal(content, document.body) : content
}

export { Tooltip, TooltipTrigger, TooltipContent, TooltipProvider }
