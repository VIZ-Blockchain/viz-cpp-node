import { defineConfig, type DefaultTheme } from 'vitepress';

const githubRepo = 'https://github.com/VIZ-Blockchain/viz-cpp-node';

// Sidebar tree shared by all locales (page paths are relative to the
// per-locale srcDir; VitePress resolves them under each locale folder).
function buildSidebar(): DefaultTheme.SidebarItem[] {
  return [
    {
      text: 'Introduction',
      items: [
        { text: 'What is VIZ Ledger?', link: '/introduction/what-is-viz' },
        { text: 'Architecture', link: '/introduction/architecture' },
        { text: 'Key Concepts', link: '/introduction/key-concepts' },
      ],
    },
    {
      text: 'Run a Node',
      items: [
        { text: 'Getting Started', link: '/node/getting-started' },
        { text: 'Configuration', link: '/node/configuration' },
        { text: 'Building from Source', link: '/node/building' },
        { text: 'Docker', link: '/node/docker' },
        { text: 'Validator Node', link: '/node/validator-node' },
        { text: 'Validator Guard', link: '/node/validator-guard' },
        { text: 'Snapshots', link: '/node/snapshot' },
        { text: 'Monitoring', link: '/node/monitoring' },
      ],
    },
    {
      text: 'Consensus',
      items: [
        { text: 'Fair-DPOS', link: '/consensus/fair-dpos' },
        { text: 'Block Processing', link: '/consensus/block-processing' },
        { text: 'Fork Resolution', link: '/consensus/fork-resolution' },
        { text: 'Emergency Consensus', link: '/consensus/emergency-consensus' },
        { text: 'Hardforks', link: '/consensus/hardforks' },
      ],
    },
    {
      text: 'P2P Network',
      items: [
        { text: 'Overview', link: '/p2p/overview' },
        { text: 'Messages', link: '/p2p/messages' },
        { text: 'Sync Scenarios', link: '/p2p/sync-scenarios' },
        { text: 'Forward Mode', link: '/p2p/forward-mode' },
        { text: 'Stats Reference', link: '/p2p/stats-reference' },
      ],
    },
    {
      text: 'Plugins',
      items: [
        { text: 'Overview', link: '/plugins/overview' },
        { text: 'Validator', link: '/plugins/validator' },
        { text: 'Snapshot', link: '/plugins/snapshot' },
        { text: 'Chain', link: '/plugins/chain' },
        { text: 'Database API', link: '/plugins/database-api' },
        { text: 'Webserver', link: '/plugins/webserver' },
      ],
    },
    {
      text: 'Protocol',
      items: [
        { text: 'Data Types', link: '/protocol/data-types' },
        { text: 'Virtual Operations', link: '/protocol/virtual-operations' },
        {
          text: 'Operations',
          collapsed: false,
          items: [
            { text: 'Overview', link: '/protocol/operations/overview' },
            { text: 'Accounts', link: '/protocol/operations/accounts' },
            { text: 'Transfers & Vesting', link: '/protocol/operations/transfers' },
            { text: 'Validators', link: '/protocol/operations/validators' },
            { text: 'Content', link: '/protocol/operations/content' },
            { text: 'Recovery', link: '/protocol/operations/recovery' },
            { text: 'Escrow', link: '/protocol/operations/escrow' },
            { text: 'Committee', link: '/protocol/operations/committee' },
            { text: 'Invites', link: '/protocol/operations/invites' },
            { text: 'Awards', link: '/protocol/operations/awards' },
            { text: 'Subscriptions', link: '/protocol/operations/subscriptions' },
            { text: 'Account Market', link: '/protocol/operations/account-market' },
            { text: 'Proposals', link: '/protocol/operations/proposals' },
          ],
        },
      ],
    },
    {
      text: 'Storage',
      items: [
        { text: 'Shared Memory', link: '/storage/shared-memory' },
        { text: 'Block Log', link: '/storage/block-log' },
        { text: 'Snapshots', link: '/storage/snapshots' },
      ],
    },
    {
      text: 'Governance',
      items: [
        { text: 'Chain Properties', link: '/governance/chain-properties' },
        { text: 'Staking & DAO', link: '/governance/staking-and-dao' },
        { text: 'Committee', link: '/governance/committee' },
      ],
    },
    {
      text: 'API Reference',
      items: [
        { text: 'JSON-RPC', link: '/api/json-rpc' },
        { text: 'CLI Wallet', link: '/api/cli-wallet' },
        { text: 'Client Libraries', link: '/api/client-libraries' },
      ],
    },
    {
      text: 'Development',
      items: [
        { text: 'Building', link: '/development/building' },
        { text: 'Testing', link: '/development/testing' },
        { text: 'Debugging', link: '/development/debugging' },
        { text: 'Plugin Development', link: '/development/plugin-development' },
      ],
    },
    {
      text: 'Advanced',
      items: [
        { text: 'Security', link: '/advanced/security' },
        { text: 'Database Schema', link: '/advanced/database-schema' },
        { text: 'DLT Architecture', link: '/advanced/dlt-architecture' },
        { text: 'Hardfork Management', link: '/advanced/hardfork-management' },
      ],
    },
  ];
}

// Prefix every internal `link` in a sidebar tree with the locale prefix
// (e.g. '/ru'). External links (http/https) are left untouched.
function prefixSidebar(items: DefaultTheme.SidebarItem[], prefix: string): DefaultTheme.SidebarItem[] {
  if (!prefix) return items;
  const walk = (nodes: DefaultTheme.SidebarItem[]): DefaultTheme.SidebarItem[] =>
    nodes.map((node) => {
      const next: DefaultTheme.SidebarItem = { ...node };
      if (typeof next.link === 'string' && next.link.startsWith('/')) {
        next.link = prefix + next.link;
      }
      if (Array.isArray(next.items)) {
        next.items = walk(next.items);
      }
      return next;
    });
  return walk(items);
}

function localizedSidebar(prefix: string): DefaultTheme.SidebarItem[] {
  return prefixSidebar(buildSidebar(), prefix);
}

function localizedNav(
  prefix: string,
  introLabel: string,
  nodeLabel: string,
  protocolLabel: string,
  apiLabel: string,
): DefaultTheme.NavItem[] {
  return [
    { text: introLabel, link: `${prefix}/introduction/what-is-viz` },
    { text: nodeLabel, link: `${prefix}/node/getting-started` },
    { text: protocolLabel, link: `${prefix}/protocol/data-types` },
    { text: apiLabel, link: `${prefix}/api/json-rpc` },
    { text: 'GitHub', link: githubRepo },
  ];
}

export default defineConfig({
  title: 'VIZ Ledger Documentation',
  description: 'Official documentation for VIZ Ledger — hybrid DLT with Fair-DPOS consensus',
  cleanUrls: true,
  lastUpdated: true,
  outDir: '../dist',
  ignoreDeadLinks: true,

  themeConfig: {
    socialLinks: [{ icon: 'github', link: githubRepo }],
    footer: {
      copyright: 'VIZ Ledger',
    },
    search: {
      provider: 'local',
    },
  },

  locales: {
    root: {
      label: 'English',
      lang: 'en-US',
      themeConfig: {
        nav: localizedNav('', 'Introduction', 'Run a Node', 'Protocol', 'API'),
        sidebar: localizedSidebar(''),
      },
    },
    ru: {
      label: 'Русский',
      lang: 'ru',
      themeConfig: {
        nav: localizedNav('/ru', 'Введение', 'Запуск узла', 'Протокол', 'API'),
        sidebar: localizedSidebar('/ru'),
      },
    },
    'zh-CN': {
      label: '中文',
      lang: 'zh-CN',
      themeConfig: {
        nav: localizedNav('/zh-CN', '简介', '运行节点', '协议', 'API'),
        sidebar: localizedSidebar('/zh-CN'),
      },
    },
  },
});
