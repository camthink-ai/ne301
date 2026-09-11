import request from '../request'

export interface LoginReq {
    // username: string,
    password: string
}

export interface LoginRes {
    success: boolean;
    data?: {
        token: string;
    };
    message?: string;
}
export interface ChangePasswordReq {
    password: string;
}
const login = {
    loginIn: (data: LoginReq): Promise<LoginRes> => request.post('/api/v1/login', data),
    changePassword: (data: ChangePasswordReq) => request.post('/api/v1/change-password', data),
    /** Verify password without showing automatic error toast (used by format confirmation etc.) */
    verifyPassword: (password: string) => request.post('/api/v1/login', { password }, { skipErrorToast: true } as any),
}

export default login;