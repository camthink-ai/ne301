import { defineConfig } from 'vitepress'
import { readdirSync, readFileSync } from 'node:fs'
import { resolve } from 'node:path'

// https://vitepress.dev/reference/site-config
//
// 双语结构：默认英文挂根路径，中文挂在 /zh/，导航栏可互相切换。
//   docs/web-api/**           English (default)
//   docs/zh/web-api/**        简体中文
export default defineConfig({
  // 部署在 https://camthink-ai.github.io/ne301/ 子路径下
  base: '/ne301/',
  title: 'NE301',
  description: 'NE301 AI camera firmware and Web API documentation',
  lang: 'en-US',

  // 内部调试/工作记录与维护说明不发布到公开站点
  srcExclude: ['**/DEBUG_*', '**/WORK_SUMMARY*', 'README.md'],

  // locale 元数据（语言切换按钮、<html lang>）与各语言的 nav/sidebar 等
  // 主题选项都必须挂在顶层 locales 上：VitePress 运行时只合并
  // locales.<key>.themeConfig，themeConfig.locales.<key> 不会被读取
  locales: {
    root: {
      label: 'English',
      lang: 'en-US',
      themeConfig: {
        nav: [
          { text: 'Home', link: '/' },
          { text: 'Web API', link: '/web-api/', activeMatch: '/web-api/' },
          { text: 'More Docs', link: '/misc/', activeMatch: '/misc|/api/|/design/' },
          { text: 'GitHub', link: 'https://github.com/camthink-ai/ne301' },
        ],
        sidebar: {
          '/web-api/': [
            {
              text: 'Getting Started',
              items: [
                { text: 'API Overview', link: '/web-api/' },
                { text: 'Authentication & Quick Start', link: '/web-api/authentication' },
              ],
            },
            {
              text: 'Endpoint Reference (auto-generated)',
              collapsed: false,
              items: endpointItems('en', '/web-api/endpoints/'),
            },
            {
              text: 'Module Guides',
              collapsed: false,
              items: [
                { text: 'Network Management', link: '/web-api/network' },
              ],
            },
          ],
          '/misc/': miscSidebar('en'),
          '/api/': miscSidebar('en'),
          '/design/': miscSidebar('en'),
          '/': miscSidebar('en'),
        },
        outline: { level: [2, 3], label: 'On this page' },
        docFooter: { prev: 'Previous', next: 'Next' },
        lastUpdated: {
          text: 'Last updated',
          formatOptions: { dateStyle: 'short', timeStyle: 'short' },
        },
        returnToTopLabel: 'Back to top',
        sidebarMenuLabel: 'Menu',
        darkModeSwitchLabel: 'Appearance',
        lightModeSwitchTitle: 'Switch to light mode',
        darkModeSwitchTitle: 'Switch to dark mode',
      },
    },
    zh: {
      label: '简体中文',
      lang: 'zh-CN',
      link: '/zh/',
      themeConfig: {
        nav: [
          { text: '首页', link: '/zh/' },
          { text: 'Web API', link: '/zh/web-api/', activeMatch: '/zh/web-api/' },
          { text: '其他文档', link: '/zh/misc/', activeMatch: '/zh/misc|/api/|/design/' },
          { text: 'GitHub', link: 'https://github.com/camthink-ai/ne301' },
        ],
        sidebar: {
          '/zh/web-api/': [
            {
              text: '入门',
              items: [
                { text: 'API 总览', link: '/zh/web-api/' },
                { text: '认证与快速开始', link: '/zh/web-api/authentication' },
              ],
            },
            {
              text: '端点参考（自动生成）',
              collapsed: false,
              items: endpointItems('zh', '/zh/web-api/endpoints/'),
            },
            {
              text: '模块详解',
              collapsed: false,
              items: [
                { text: '网络管理', link: '/zh/web-api/network' },
              ],
            },
          ],
          '/zh/misc/': miscSidebar('zh'),
          '/zh/api/': miscSidebar('zh'),
          '/zh/design/': miscSidebar('zh'),
          '/zh/': miscSidebar('zh'),
        },
        outline: { level: [2, 3], label: '本页目录' },
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
    },
  },

  head: [['link', { rel: 'icon', type: 'image/svg+xml', href: '/ne301/logo.svg' }]],

  // 各语言共用的主题选项；语言相关内容在上面 locales.<key>.themeConfig
  themeConfig: {
    socialLinks: [
      { icon: 'github', link: 'https://github.com/camthink-ai/ne301' },
    ],

    footer: {
      message: 'Released under the MIT License.',
      copyright: 'Copyright © 2026 NE301 Contributors',
    },

    search: {
      provider: 'local',
      options: {
        locales: {
          zh: {
            translations: {
              button: { buttonText: '搜索文档', buttonAriaLabel: '搜索文档' },
              modal: {
                noResultsText: '无法找到相关结果',
                resetButtonTitle: '清除查询条件',
                displayDetails: '显示明细',
                footer: { selectText: '选择', navigateText: '切换', closeText: '关闭' },
              },
            },
          },
        },
      },
    },
  },
})

/**
 * 扫描对应语言的 endpoints 目录构建 sidebar。
 * 新增 API 模块重新生成端点文档后，会自动出现在这里。
 */
function endpointItems(lang: 'en' | 'zh', linkPrefix: string) {
  const dir = resolve(process.cwd(), lang === 'en' ? 'web-api/endpoints' : 'zh/web-api/endpoints')
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
      const name = raw.match(/^title:\s*(.+)$/m)?.[1]?.trim() ?? slug
      return { text: name, link: `${linkPrefix}${slug}`, order: order.indexOf(slug) }
    })
    .sort((a, b) => (a.order < 0 ? 999 : a.order) - (b.order < 0 ? 999 : b.order))
    .map(({ text, link }) => ({ text, link }))
}

/**
 * 既有专题文档（api/、design/、根目录说明文档，中文撰写）两侧共用；
 * 英文侧边栏标题标注语言。
 */
function miscSidebar(lang: 'en' | 'zh') {
  const zh = lang === 'zh'
  return [
    {
      text: zh ? '开发文档' : 'Developer Docs',
      collapsed: false,
      items: [
        { text: zh ? '文档索引' : 'Doc Index', link: zh ? '/zh/misc/' : '/misc/' },
        { text: zh ? '拍照上传流程' : 'Photo Capture & Upload Flow (中文)', link: '/拍照上传流程' },
        { text: zh ? 'RTMP API' : 'RTMP API (中文)', link: '/api/RTMP_API' },
        { text: zh ? 'PoE Network API' : 'PoE Network API (中文)', link: '/api/PoE_Network_API' },
        { text: zh ? 'PIR Sensor API' : 'PIR Sensor API (中文)', link: '/api/PIR_SENSOR_API' },
        { text: zh ? '视频流中台升级设计' : 'Video Stream Hub Design (中文)', link: '/design/VIDEO_STREAM_HUB_UPGRADE' },
      ],
    },
  ]
}
