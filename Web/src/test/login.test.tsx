import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { fireEvent, render, screen, waitFor } from '@testing-library/preact';
import Login from '@/pages/login';

const { loginIn, setToken, getAPConfig, getDeviceInfo, navigate } = vi.hoisted(() => ({
  loginIn: vi.fn(),
  setToken: vi.fn(),
  getAPConfig: vi.fn(),
  getDeviceInfo: vi.fn(),
  navigate: vi.fn(),
}));

vi.mock('@lingui/react', () => ({ useLingui: () => ({ i18n: { _: (key: string) => key } }) }));
vi.mock('sonner', () => ({ toast: { success: vi.fn(), error: vi.fn() } }));
vi.mock('@/components/ui/label', () => ({ Label: 'label' }));
vi.mock('@/services/api/login', () => ({ default: { loginIn } }));
vi.mock('@/store/auth', () => ({ useAuthStore: () => ({ setToken }) }));
vi.mock('@/store/communicationData', () => ({ useCommunicationData: () => ({ getAPConfig }) }));
vi.mock('@/store/systemInfo', () => ({ useSystemInfo: () => ({ getDeviceInfo }) }));
vi.mock('react-router-dom', () => ({ useNavigate: () => navigate }));

const renderLogin = () => {
  const view = render(<Login />);
  const form = view.container.querySelector('form') as HTMLFormElement;
  const password = view.container.querySelector('input[type="password"]') as HTMLInputElement;
  const button = screen.getByRole('button', { name: /login/i }) as HTMLButtonElement;
  return { ...view, form, password, button };
};

const enterValidPassword = (password: HTMLInputElement) => {
  fireEvent.change(password, { target: { value: 'Valid1234' } });
};

describe('login form submission', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.spyOn(console, 'error').mockImplementation(() => {});
    loginIn.mockResolvedValue({ success: false });
    getAPConfig.mockResolvedValue(undefined);
    getDeviceInfo.mockResolvedValue(undefined);
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('associates Login with the form as its implicit submitter', () => {
    const { form, button } = renderLogin();

    expect(button.form).toBe(form);
  });

  it('submits valid form data through the authentication service once', async () => {
    const { form, password } = renderLogin();
    enterValidPassword(password);

    form.requestSubmit();

    await waitFor(() => expect(loginIn).toHaveBeenCalledTimes(1));
    expect(loginIn).toHaveBeenCalledWith({ password: 'Valid1234' });
  });

  it('routes a Login click through the same authentication call once', async () => {
    const { button, password } = renderLogin();
    enterValidPassword(password);

    fireEvent.click(button);

    await waitFor(() => expect(loginIn).toHaveBeenCalledTimes(1));
    expect(loginIn).toHaveBeenCalledWith({ password: 'Valid1234' });
  });

  it.each([
    ['empty form submission', '', 'form'],
    ['short form submission', 'short', 'form'],
    ['empty Login click', '', 'click'],
    ['short Login click', 'short', 'click'],
  ])('does not authenticate for %s', async (_name, value, action) => {
    const { form, password, button } = renderLogin();
    fireEvent.change(password, { target: { value } });

    if (action === 'form') {
      form.requestSubmit();
    } else {
      fireEvent.click(button);
    }

    await waitFor(() => expect(loginIn).not.toHaveBeenCalled());
    expect(password.className).toContain('border-red-500');
  });

  it('does not navigate or set a token when the server rejects credentials', async () => {
    loginIn.mockResolvedValue({ success: false, message: 'invalid credentials' });
    const { form, password } = renderLogin();
    enterValidPassword(password);

    form.requestSubmit();

    await waitFor(() => expect(loginIn).toHaveBeenCalledTimes(1));
    expect(setToken).not.toHaveBeenCalled();
    expect(navigate).not.toHaveBeenCalled();
    expect(getAPConfig).not.toHaveBeenCalled();
    expect(getDeviceInfo).not.toHaveBeenCalled();
  });

  it('does not report success when the authentication request fails', async () => {
    loginIn.mockRejectedValue(new Error('network failure'));
    const { form, password } = renderLogin();
    enterValidPassword(password);

    form.requestSubmit();

    await waitFor(() => expect(loginIn).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(setToken).not.toHaveBeenCalled());
    expect(navigate).not.toHaveBeenCalled();
  });

  it('preserves token initialization and navigation after successful authentication', async () => {
    loginIn.mockResolvedValue({ success: true, data: { token: 'device-token' } });
    const { form, password } = renderLogin();
    enterValidPassword(password);

    form.requestSubmit();

    await waitFor(() => expect(setToken).toHaveBeenCalledWith(`Basic ${window.btoa('admin:Valid1234')}`));
    expect(navigate).toHaveBeenCalledWith('/');
    expect(getAPConfig).toHaveBeenCalledTimes(1);
    expect(getDeviceInfo).toHaveBeenCalledTimes(1);
  });

  it('ignores repeated form submissions and clicks while authentication is pending', async () => {
    let resolveLogin: (value: { success: boolean }) => void = () => {};
    loginIn.mockImplementation(() => new Promise((resolve) => { resolveLogin = resolve; }));
    const { form, password, button } = renderLogin();
    enterValidPassword(password);

    form.requestSubmit();
    form.requestSubmit();
    fireEvent.click(button);

    await waitFor(() => expect(loginIn).toHaveBeenCalledTimes(1));
    expect(button).toBeDisabled();

    resolveLogin({ success: false });
    await waitFor(() => expect(button).not.toBeDisabled());
  });

  it('keeps the password visibility control separate from form submission', async () => {
    const { container, password } = renderLogin();
    const visibilityButton = container.querySelector('button[type="button"]') as HTMLButtonElement;

    fireEvent.click(visibilityButton);

    expect(password.type).toBe('text');
    expect(loginIn).not.toHaveBeenCalled();
    expect(visibilityButton.type).toBe('button');
  });
});
