import { useNavigate } from 'react-router-dom';
import { useLingui } from '@lingui/react';
import { Button } from '@/components/ui/button';
import SvgIcon from '@/components/svg-icon';

// Reached when a firmware upgrade (e.g. WiFi) is still in progress after the
// frontend's wait window elapsed. The device reboots and loads new firmware
// during the upgrade and is unreachable for a while; guide the user to read
// the device LED and reconnect / wake accordingly.
export default function UpgradeWaiting() {
    const { i18n } = useLingui();
    const navigate = useNavigate();

    const ledItems = [
        {
            dot: 'animate-pulse bg-yellow-500',
            text: i18n._('sys.system_management.upgrade_led_blinking'),
        },
        {
            dot: 'bg-green-500',
            text: i18n._('sys.system_management.upgrade_led_steady'),
        },
        {
            dot: 'bg-gray-400',
            text: i18n._('sys.system_management.upgrade_led_other'),
        },
    ];

    return (
        <div className="w-full h-full flex items-center justify-center p-6">
            <div className="md:max-w-lg w-full flex flex-col items-center gap-4">
                <div className="w-16 h-16 rounded-full bg-yellow-100 dark:bg-yellow-900/40 flex items-center justify-center">
                    <SvgIcon
                      icon="clock"
                      className="w-9 h-9 text-yellow-600 dark:text-yellow-400"
                    />
                </div>
                <h2 className="text-lg font-semibold text-text-primary text-center">
                    {i18n._('sys.system_management.upgrade_in_progress_title')}
                </h2>
                <p className="text-sm text-text-secondary text-center">
                    {i18n._('sys.system_management.upgrade_in_progress_intro')}
                </p>

                <div className="w-full rounded-lg border border-gray-200 dark:border-gray-700 p-4 flex flex-col gap-3">
                    <p className="text-sm font-medium text-text-primary">
                        {i18n._('sys.system_management.upgrade_led_title')}
                    </p>
                    <ul className="flex flex-col gap-3">
                        {ledItems.map((item) => (
                            <li
                              key={item.text}
                              className="flex items-start gap-3"
                            >
                                <span
                                  className={`mt-1.5 inline-block w-3 h-3 rounded-full shrink-0 ${item.dot}`}
                                />
                                <span className="text-sm text-text-secondary">
                                    {item.text}
                                </span>
                            </li>
                        ))}
                    </ul>
                </div>

                <div className="flex gap-2 mt-2">
                    <Button
                      variant="outline"
                      onClick={() => navigate('/login')}
                    >
                        {i18n._('sys.system_management.relogin')}
                    </Button>
                    <Button
                      variant="primary"
                      onClick={() => navigate('/device-tool')}
                    >
                        {i18n._('sys.system_management.reconnect_device')}
                    </Button>
                </div>
            </div>
        </div>
    );
}
