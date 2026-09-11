import { useState, useEffect } from 'preact/hooks'

import SvgIcon from '@/components/svg-icon'
import { useLingui } from '@lingui/react'
import { createPortal } from 'preact/compat'
import { useLanguage } from '@/hooks/useLanguage'
import { Separator } from '@/components/ui/separator'
import { Button } from '@/components/ui/button'
import { Link } from 'react-router-dom'
import { toast } from 'sonner'
import systemSettings from '@/services/api/systemSettings'
// import { useAuthStore } from '@/store/auth'

type NavRightProps = {
    onClose: () => void
    handleOpenLogs: () => void
}

export default function NavRight({ onClose, handleOpenLogs }: NavRightProps) {
    const { i18n } = useLingui()
    const { locale, setLocale } = useLanguage()
    const [openLocales, setOpenLocales] = useState(false)
    const [isVisible, setIsVisible] = useState(false)
    const [isAnimating, setIsAnimating] = useState(false)
    const [isRestartDialogOpen, setIsRestartDialogOpen] = useState(false)
    const [restartLoading, setRestartLoading] = useState(false)
    // const { isValidateToken } = useAuthStore()

    const handleRestartDevice = async () => {
        try {
            setRestartLoading(true)
            await systemSettings.restartDevice({ delaySeconds: 2 })
            setIsRestartDialogOpen(false)
            toast.success(i18n._('sys.system_management.device_restarting'))
        } catch (error) {
            // eslint-disable-next-line no-console
            console.error(error)
        } finally {
            setRestartLoading(false)
        }
    }

    // Start animation when component mounts
    useEffect(() => {
        setIsVisible(true)
        setIsAnimating(true)

        // Prevent background scrolling
        document.body.style.overflow = 'hidden'

        return () => {
            document.body.style.overflow = 'unset'
        }
    }, [])

    const handleClose = () => {
        setIsAnimating(false)
        // Wait for animation to complete before closing
        setTimeout(() => {
            onClose()
        }, 300)
    }

    const handleKeyDown = (e: KeyboardEvent) => {
        if (e.key === 'Escape') handleClose()
    }

    const changeLang = (lng: 'zh' | 'en') => {
        if (lng !== locale) setLocale(lng)
        setOpenLocales(false)
    }

    return createPortal(
        <div
          className={`fixed inset-0 z-[1000] pointer-events-auto transition-opacity duration-300 ${isVisible ? 'opacity-100' : 'opacity-0'
                }`}
          aria-hidden={!isVisible}
        >
            {/* Mask */}
            <div
              className={`absolute inset-0 bg-black/40 transition-opacity duration-300 ${isAnimating ? 'opacity-100' : 'opacity-0'
                    }`}
              role="button"
              tabIndex={0}
              aria-label="Close menu overlay"
              onClick={handleClose}
              onKeyDown={handleKeyDown}
            />
            {/* Drawer */}
            <div
              className={`absolute right-0 top-0 h-full w-72 bg-white shadow-xl transition-transform duration-300 ease-out will-change-transform ${isAnimating ? 'translate-x-0' : 'translate-x-full'
                    }`}
              role="dialog"
              aria-modal="true"
            >
                <div className=" relative">
                    <Button variant="ghost" className="my-4" onClick={handleClose}>
                        <SvgIcon icon="menu_close" />
                    </Button>
                    <ul className="space-y-4">
                        <Separator />
                        {/* eslint-disable-next-line jsx-a11y/click-events-have-key-events, jsx-a11y/no-static-element-interactions */}
                        <li className="flex justify-between items-center px-4 cursor-pointer" onClick={() => setIsRestartDialogOpen(true)}>
                            <div className="flex items-center space-x-2">
                                <div className="w-6 h-6">
                                    <SvgIcon className="w-6 h-6 text-text-secondary" icon="restart" />
                                </div>
                                <span className="text-base text-text-primary">{i18n._('sys.header.restart_device')}</span>
                            </div>
                        </li>
                        <Separator />
                        <li className="flex justify-between items-center px-4" onClick={() => handleOpenLogs()}>
                            <div className="flex items-center space-x-2">
                                <div className="w-6 h-6 cursor-pointer">
                                    <SvgIcon className="w-6 h-6 text-text-secondary" icon="logs" />
                                </div>
                                <span className="text-base text-text-primary">{i18n._('sys.header.header_logs')}</span>
                            </div>
                        </li>
                        <Separator />
                        <li className="px-4">
                            {/* eslint-disable-next-line jsx-a11y/click-events-have-key-events, jsx-a11y/no-static-element-interactions */}
                            <div className="flex justify-between items-center" onClick={() => setOpenLocales(!openLocales)}>
                                <div className="flex flex-1 items-center space-x-2">
                                    <div className="w-6 h-6">
                                        <SvgIcon className="w-6 h-6" icon="locales" />
                                    </div>
                                    <span className="text-base text-text-primary">{i18n._('sys.header.header_language')}</span>
                                </div>
                                <div className="w-6 h-6">
                                    <SvgIcon icon="bottom" className={`${openLocales ? 'rotate-180' : 'rotate-0'} transition-transform duration-300 ease-in-out transform-gpu w-full h-full`} />
                                </div>
                            </div>
                            {/* Language selection panel: only add expand/collapse animation */}
                            <div
                              id="locale-sub"
                              className={`overflow-hidden transition-[max-height,opacity] duration-300 ease-in-out ${openLocales ? 'max-h-24 opacity-100' : 'max-h-0 opacity-0'}`}
                            >
                                <div className="ml-8">
                                    {/* eslint-disable-next-line jsx-a11y/click-events-have-key-events, jsx-a11y/no-static-element-interactions */}
                                    <div className="flex items-center space-x-2 my-2" onClick={() => changeLang('zh')}>
                                        中文
                                    </div>
                                    {/* eslint-disable-next-line jsx-a11y/click-events-have-key-events, jsx-a11y/no-static-element-interactions */}
                                    <div className="flex items-center space-x-2 mt-2" onClick={() => changeLang('en')}>
                                        English
                                    </div>
                                </div>
                            </div>

                        </li>
                        <Separator />
                        <li>
                            <Link className="flex justify-between items-center px-4" to="https://github.com/camthink-ai" target="_blank">
                            
                            <div className="flex flex-1 items-center space-x-2">
                                <div className="w-6 h-6">
                                    <SvgIcon className="w-6 h-6" icon="github2" />
                                </div>
                                <span className="text-base text-text-primary">{i18n._('sys.header.header_github')}</span>
                            </div>
                            <div>
                            <SvgIcon className="w-6 h-6" icon="link" />
                            </div>
                            </Link>
                        </li>
                        <Separator />
                        <li>
                            <Link className="flex justify-between items-center px-4" to="https://wiki.camthink.ai/docs/" target="_blank">
                                <div className="flex items-center space-x-2">
                                    <div className="w-6 h-6">
                                        <SvgIcon className="w-6 h-6" icon="hint2" />
                                    </div>
                                    <span className="text-base text-text-primary">{i18n._('sys.header.header_help_center')}</span>
                                </div>
                                <div>
                                    <SvgIcon className="w-6 h-6" icon="link" />
                                </div>
                            </Link>
                        </li>
                        <Separator />
                    </ul>
                </div>
            </div>

            {/* Restart confirmation — rendered inside this z-[1000] overlay (not
             * the shared Dialog, whose z-50 portal would be covered by the drawer) */}
            {isRestartDialogOpen && (
                <div
                  className="absolute inset-0 z-10 flex items-center justify-center bg-black/40 p-4"
                  role="dialog"
                  aria-modal="true"
                  onClick={() => { if (!restartLoading) setIsRestartDialogOpen(false); }}
                >
                    <div
                      className="w-full max-w-sm bg-white rounded-lg shadow-2xl p-6"
                      onClick={(e) => e.stopPropagation()}
                    >
                        <p className="text-base font-semibold text-text-primary">
                            {i18n._('sys.header.restart_device_confirm_title')}
                        </p>
                        <p className="text-sm text-text-secondary mt-3">
                            {i18n._('sys.header.restart_device_confirm')}
                        </p>
                        <div className="flex justify-end gap-3 mt-6">
                            <Button
                              variant="outline"
                              disabled={restartLoading}
                              onClick={() => setIsRestartDialogOpen(false)}
                            >
                                {i18n._('common.cancel')}
                            </Button>
                            <Button
                              variant="primary"
                              disabled={restartLoading}
                              onClick={() => handleRestartDevice()}
                            >
                                {i18n._('common.confirm')}
                            </Button>
                        </div>
                    </div>
                </div>
            )}
        </div>,
        document.body
    )
}