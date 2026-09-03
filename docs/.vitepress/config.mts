import { defineConfig } from 'vitepress'
import { readdirSync, readFileSync } from 'node:fs'
import { resolve } from 'node:path'

// https://vitepress.dev/reference/site-config
export default defineConfig({
  // 部署在 https://camthink-ai.github.io/ne301/ 子路径下
  base: '/ne301/',
  title: 'NE301',
  description: 'NE301 AI 摄像头固件与 Web API 文档',
  lang: 'zh-CN',

  // 内部调试/工作记录与维护说明不发布到公开站点
  srcExclude: ['**/DEBUG_*', '**/WORK_SUMMARY*', 'README.md'],

  head: [['link', { rel: 'icon', type: 'image/svg+xml', href: '/ne301/logo.svg' }]],

  themeConfig: {
    nav: [
      { text: '首页', link: '/' },
      { text: 'Web API', link: '/web-api/', activeMatch: '/web-api/' },
      { text: '其他文档', link: '/misc/', activeMatch: '/misc|/api/|/design/' },
      {
        text: 'GitHub',
        link: 'https://github.com/camthink-ai/ne301',
      },
    ],

    sidebar: {
      '/web-api/': [
        {
          text: '入门',
          items: [
            { text: 'API 总览', link: '/web-api/' },
            { text: '认证与快速开始', link: '/web-api/authentication' },
          ],
        },
        {
          text: '端点参考（自动生成）',
          collapsed: false,
          items: endpointItems(),
        },
        {
          text: '模块详解',
          collapsed: false,
          items: [
            { text: '网络管理', link: '/web-api/network' },
            { text: 'RTMP 推流', link: '/api/RTMP_API' },
            { text: 'PIR 传感器', link: '/api/PIR_SENSOR_API' },
            { text: 'PoE 配电', link: '/api/PoE_Network_API' },
          ],
        },
      ],
      '/misc/': miscSidebar(),
      // 老路径兜底，避免从旧链接进来时侧边栏丢失
      '/api/': miscSidebar(),
      '/design/': miscSidebar(),
      '/': miscSidebar(),
    },

    outline: { level: [2, 3], label: '本页目录' },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/camthink-ai/ne301' },
    ],

    footer: {
      message: '基于 MIT License 发布',
      copyright: 'Copyright © 2026 NE301 Contributors',
    },

    docFooter: { prev: '上一页', next: '下一页' },
    lastUpdated: {
      text: '最后更新',
      formatOptions: { dateStyle: 'short', timeStyle: 'short' },
    },
    returnToTopLabel: '回到顶部',
    sidebarMenuLabel: '菜单',
    darkModeSwitchLabel: '主题',
    lightModeSwitchTitle: '切换到浅色模式',
    darkModeSwitchTitle: '切换到深色模式',
  },
})

/**
 * 扫描 docs/web-api/endpoints/ 下的自动生成页面构建 sidebar。
 * 新增 API 模块重新生成端点文档后，会自动出现在这里。
 */
function endpointItems() {
  // vitepress dev/build 的 cwd 始终是 docs/（config 被 bundle 后
  // import.meta.url 指向 .vitepress/ 下的临时文件，不可用）
  const dir = resolve(process.cwd(), 'web-api/endpoints')
  const order = [
    'auth', 'network', 'device', 'work_mode', 'capture', 'preview',
    'isp', 'rtmp', 'rtsp', 'file', 'mqtt', 'webhook', 'ota',
    'ai_management', 'model_validation',
  ]
  return readdirSync(dir)
    .filter((f) => f.endsWith('.md') && f !== 'index.md')
    .map((f) => {
      const slug = f.replace(/\.md$/, '')
      const raw = readFileSync(resolve(dir, f), 'utf-8')
      const name =
        raw.match(/^title:\s*(.+)$/m)?.[1]?.trim() ?? slug
      return {
        text: name,
        link: `/web-api/endpoints/${slug}`,
        order: order.indexOf(slug),
      }
    })
    .sort((a, b) => (a.order < 0 ? 999 : a.order) - (b.order < 0 ? 999 : b.order))
    .map(({ text, link }) => ({ text, link }))
}

/** 既有文档（api/、design/、根目录说明文档）的侧边栏 */
function miscSidebar() {
  return [
    {
      text: '开发文档',
      collapsed: false,
      items: [
        { text: '文档索引', link: '/misc/' },
        { text: '拍照上传流程', link: '/拍照上传流程' },
        { text: 'RTMP API', link: '/api/RTMP_API' },
        { text: 'PoE Network API', link: '/api/PoE_Network_API' },
        { text: 'PIR Sensor API', link: '/api/PIR_SENSOR_API' },
        { text: '视频流中台升级设计', link: '/design/VIDEO_STREAM_HUB_UPGRADE' },
      ],
    },
  ]
}
