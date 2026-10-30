import { useState, useRef } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { useNavigate } from 'react-router-dom'
import {
  Card,
  CardContent,
  CardFooter,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from "@/components/ui/label";
import { toast } from 'sonner'
import SvgIcon from '@/components/svg-icon'
import login, { type LoginRes } from '@/services/api/login'
import { useAuthStore } from '@/store/auth';
import { useCommunicationData } from '@/store/communicationData';
import { useSystemInfo } from '@/store/systemInfo';

export default function Login() {
  const { i18n } = useLingui();
  const navigate = useNavigate();
  const { setToken } = useAuthStore();
  const { getAPConfig } = useCommunicationData();
  const { getDeviceInfo } = useSystemInfo();
  const [isPasswordVisible, setIsPasswordVisible] = useState(false);
  const usernameRef = useRef<HTMLInputElement>(null);
  const passwordRef = useRef<HTMLInputElement>(null);
  const isSubmittingRef = useRef(false);
  const [isLoading, setIsLoading] = useState(false);
  const [errors, setErrors] = useState<{ username?: string; password?: string }>({});

  const handlePasswordVisible = (e: MouseEvent) => {
    e.preventDefault();
    setIsPasswordVisible(!isPasswordVisible);
  }

  const validateForm = () => {
    const username = usernameRef.current?.value;
    const password = passwordRef.current?.value;
    const newErrors: { username?: string; password?: string } = {};

    if (!username) {
      newErrors.username = i18n._('sys.login.username_error');
    } else if (username.includes(' ')) {
      newErrors.username = i18n._('sys.login.username_password_illegal_error');
    }

    if (!password) {
      newErrors.password = i18n._('sys.login.password_error');
    } else if (password.length < 8 || password.length > 32) {
      newErrors.password = i18n._('sys.login.password_length_error');
    } else if (password.includes(' ')) {
      newErrors.password = i18n._('sys.login.password_illegal_error');
    }

    setErrors(newErrors);
    return Object.keys(newErrors).length === 0;
  }

  const handleLogin = async () => {
    if (isSubmittingRef.current || !validateForm()) {
      return;
    }

    isSubmittingRef.current = true;
    setIsLoading(true);

    const username = usernameRef.current?.value || '';
    const password = passwordRef.current?.value || '';
    try {
      const res: LoginRes = await login.loginIn({
        // username,
        password
      });

      if (res.success) {
        toast.success(i18n._('errors.basic.login_success'));
        navigate('/');
        const credentials = `${username}:${password}`;
        const token = `Basic ${window.btoa(credentials)}`
        if (token) {
          setToken(token);
        }
        await getAPConfig();
        await getDeviceInfo();
      }
    } catch (error) {
      console.error(error)
    } finally {
      isSubmittingRef.current = false;
      setIsLoading(false);
    }
  }

  return (
    <div className="h-full w-full flex items-center justify-center bg-gray-100">
      <Card className="md:w-sm w-full mx-4 mb-16">
        <CardHeader className="flex flex-row items-center justify-center">
          <CardTitle className="text-text-primary text-2xl font-bold">{i18n._('sys.login.login_title')}</CardTitle>
        </CardHeader>
        <CardContent>
          <form id="login-form" onSubmit={(e) => { e.preventDefault(); handleLogin(); }}>
            <div className="flex flex-col gap-6">
              <div className="grid gap-2">
                <Label htmlFor="username">{i18n._('common.username')}</Label>
                <Input
                  ref={usernameRef}
                  id="username"
                  type="text"
                  defaultValue="admin"
                  placeholder={i18n._('common.please_enter')}
                  required
                  autoFocus={false}
                  disabled
                  className={errors.username ? "border-red-500" : ""}
                />
                {errors.username && (
                  <p className="text-sm text-red-500">{errors.username}</p>
                )}
              </div>
              <div className="grid gap-2 relative">
                <div className="flex items-center">
                  <Label htmlFor="password">{i18n._('common.password')}</Label>
                </div>
                <div className="flex items-center justify-center relative">
                  <Input
                    ref={passwordRef}
                    type={isPasswordVisible ? 'text' : 'password'}
                    placeholder={i18n._('common.please_enter')}
                    className={`pr-12 ${errors.password ? "border-red-500" : ""}`}
                    disabled={isLoading}
                  />
                  <button
                    type="button"
                    onClick={handlePasswordVisible}
                    className="absolute top-1/2 -translate-y-1/2 right-0 flex items-center bg-transparent pr-4 border-none cursor-pointer disabled:opacity-50"
                    disabled={isLoading}
                  >
                    {isPasswordVisible ? (
                      <SvgIcon className="w-5 h-5" icon="visibility" />
                    ) : (
                      <SvgIcon className="w-5 h-5" icon="visibility_off" />
                    )}
                  </button>
                </div>
                {errors.password && (
                  <p className="text-sm text-red-500">{errors.password}</p>
                )}
              </div>
            </div>
          </form>
        </CardContent>
        <CardFooter className="flex-col gap-2">
          <Button
            type="submit"
            form="login-form"
            variant="primary"
            className="w-full"
            disabled={isLoading}
          >
            {isLoading ? `${i18n._('common.loading')}...` : i18n._('common.login')}
          </Button>
        </CardFooter>
      </Card>
    </div>
  );
}
