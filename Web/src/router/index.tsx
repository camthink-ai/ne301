import { createBrowserRouter, type RouteObject, Navigate } from "react-router-dom";
import Login from "@/pages/login";
import DeviceTool from "@/pages/deviceTool";
import Layout from "@/layout";
import Home from "@/pages/home";
import NotFound from "@/pages/notFound";
import ModelVerification from "@/pages/modelVerification";
import ApplicationManagement from "@/pages/applicationManagement";
import HardwareManagement from "@/pages/hardwareManagement";
import CaptureSettings from "@/pages/captureSettings";
import SystemSettings from "@/pages/systemSettings";
import StorageManagement from "@/pages/storageManagement";
import DeviceInformation from "@/pages/deviceInformation";
import AuthGuard from "./components/auth-guard";
import ImportFSBL from "@/pages/ImportFSBL";
import ImportWifi from "@/pages/ImportWifi";
import UpgradeWaiting from "@/pages/UpgradeWaiting";

const baseRoutes = [
  {
    path: "/home",
    element: <Home />,
  },
  {
    path: "/device-tool",
    element: <DeviceTool />,
  },

  {
    path: "/model-verification",
    element: <ModelVerification />,
  },

  {
    path: "/application-management",
    element: <ApplicationManagement />,
  },

  {
    path: "/hardware-management",
    element: <HardwareManagement />,
  },

  {
    path: "/capture-settings",
    element: <CaptureSettings />,
  },

  {
    path: "/system-settings",
    element: <SystemSettings />,
  },

  {
    path: "/storage-management",
    element: <StorageManagement />,
  },

  {
    path: "/device-information",
    element: <DeviceInformation />,
  },
  {
    path: "/import-fsbl",
    element: <ImportFSBL />,
  },
  {
    path: "/import-wifi",
    element: <ImportWifi />,
  },
  {
    path: "/upgrade-waiting",
    element: <UpgradeWaiting />,
  },
];

const checkBaseRoutes = (routes: RouteObject[]) => routes.map((route) => ({
  ...route,
  element: <AuthGuard>{route.element}</AuthGuard>,
}));

// Route configuration
const router = createBrowserRouter([
  {
    path: "/",
    element: <Layout />,
    children: [
      {
        index: true,
        element: <Navigate to="/device-tool" replace />,
      },
      {
        path: "/login",
        element: <Login />,
      },
      ...checkBaseRoutes(baseRoutes),
    ],
  },
  {
    path: "*",
    element: <NotFound />,
  },
]);

export default router;
