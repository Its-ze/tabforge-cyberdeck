import type { CapacitorConfig } from '@capacitor/cli';

const config: CapacitorConfig = {
  appId: 'dev.itsz.tabforge.companion',
  appName: 'TabForge Companion',
  webDir: 'dist',
  bundledWebRuntime: false,
  android: {
    buildOptions: {
      releaseType: 'APK',
    },
  },
};

export default config;
