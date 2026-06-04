import { defineConfig, type DefaultTheme } from 'vitepress';

const githubRepo = 'https://github.com/VIZ-Blockchain/viz-cpp-node';

// ─── Sidebar translations ───────────────────────────────────────────────────

interface SidebarLabels {
  introduction: string;
  whatIsViz: string;
  architecture: string;
  keyConcepts: string;
  runANode: string;
  gettingStarted: string;
  configuration: string;
  building: string;
  docker: string;
  validatorNode: string;
  validatorGuard: string;
  snapshots: string;
  monitoring: string;
  consensus: string;
  fairDpos: string;
  blockProcessing: string;
  forkResolution: string;
  emergencyConsensus: string;
  hardforks: string;
  p2pNetwork: string;
  overview: string;
  messages: string;
  syncScenarios: string;
  forwardMode: string;
  statsReference: string;
  plugins: string;
  validator: string;
  snapshot: string;
  chain: string;
  databaseApi: string;
  webserver: string;
  protocol: string;
  dataTypes: string;
  virtualOperations: string;
  operations: string;
  accounts: string;
  transfersVesting: string;
  validators: string;
  content: string;
  recovery: string;
  escrow: string;
  committee: string;
  invites: string;
  awards: string;
  subscriptions: string;
  accountMarket: string;
  proposals: string;
  storage: string;
  sharedMemory: string;
  blockLog: string;
  snapshotsStorage: string;
  governance: string;
  chainProperties: string;
  stakingDao: string;
  committeeGov: string;
  apiReference: string;
  jsonRpc: string;
  cliWallet: string;
  clientLibraries: string;
  development: string;
  buildingDev: string;
  testing: string;
  debugging: string;
  pluginDevelopment: string;
  advanced: string;
  security: string;
  databaseSchema: string;
  dltArchitecture: string;
  hardforkManagement: string;
}

const en: SidebarLabels = {
  introduction: 'Introduction',
  whatIsViz: 'What is VIZ Ledger?',
  architecture: 'Architecture',
  keyConcepts: 'Key Concepts',
  runANode: 'Run a Node',
  gettingStarted: 'Getting Started',
  configuration: 'Configuration',
  building: 'Building from Source',
  docker: 'Docker',
  validatorNode: 'Validator Node',
  validatorGuard: 'Validator Guard',
  snapshots: 'Snapshots',
  monitoring: 'Monitoring',
  consensus: 'Consensus',
  fairDpos: 'Fair-DPOS',
  blockProcessing: 'Block Processing',
  forkResolution: 'Fork Resolution',
  emergencyConsensus: 'Emergency Consensus',
  hardforks: 'Hardforks',
  p2pNetwork: 'P2P Network',
  overview: 'Overview',
  messages: 'Messages',
  syncScenarios: 'Sync Scenarios',
  forwardMode: 'Forward Mode',
  statsReference: 'Stats Reference',
  plugins: 'Plugins',
  validator: 'Validator',
  snapshot: 'Snapshot',
  chain: 'Chain',
  databaseApi: 'Database API',
  webserver: 'Webserver',
  protocol: 'Protocol',
  dataTypes: 'Data Types',
  virtualOperations: 'Virtual Operations',
  operations: 'Operations',
  accounts: 'Accounts',
  transfersVesting: 'Transfers & Vesting',
  validators: 'Validators',
  content: 'Content',
  recovery: 'Recovery',
  escrow: 'Escrow',
  committee: 'Committee',
  invites: 'Invites',
  awards: 'Awards',
  subscriptions: 'Subscriptions',
  accountMarket: 'Account Market',
  proposals: 'Proposals',
  storage: 'Storage',
  sharedMemory: 'Shared Memory',
  blockLog: 'Block Log',
  snapshotsStorage: 'Snapshots',
  governance: 'Governance',
  chainProperties: 'Chain Properties',
  stakingDao: 'Staking & DAO',
  committeeGov: 'Committee',
  apiReference: 'API Reference',
  jsonRpc: 'JSON-RPC',
  cliWallet: 'CLI Wallet',
  clientLibraries: 'Client Libraries',
  development: 'Development',
  buildingDev: 'Building',
  testing: 'Testing',
  debugging: 'Debugging',
  pluginDevelopment: 'Plugin Development',
  advanced: 'Advanced',
  security: 'Security',
  databaseSchema: 'Database Schema',
  dltArchitecture: 'DLT Architecture',
  hardforkManagement: 'Hardfork Management',
};

const ru: SidebarLabels = {
  introduction: 'Введение',
  whatIsViz: 'Что такое VIZ Ledger?',
  architecture: 'Архитектура',
  keyConcepts: 'Ключевые понятия',
  runANode: 'Запуск узла',
  gettingStarted: 'Быстрый старт',
  configuration: 'Конфигурация',
  building: 'Сборка из исходников',
  docker: 'Docker',
  validatorNode: 'Узел валидатора',
  validatorGuard: 'Страж валидатора',
  snapshots: 'Снимки',
  monitoring: 'Мониторинг',
  consensus: 'Консенсус',
  fairDpos: 'Fair-DPOS',
  blockProcessing: 'Обработка блоков',
  forkResolution: 'Разрешение форков',
  emergencyConsensus: 'Экстренный консенсус',
  hardforks: 'Хардфорки',
  p2pNetwork: 'Сеть P2P',
  overview: 'Обзор',
  messages: 'Сообщения',
  syncScenarios: 'Сценарии синхронизации',
  forwardMode: 'Режим пересылки',
  statsReference: 'Справка по статистике',
  plugins: 'Плагины',
  validator: 'Валидатор',
  snapshot: 'Снимок',
  chain: 'Chain',
  databaseApi: 'Database API',
  webserver: 'Вебсервер',
  protocol: 'Протокол',
  dataTypes: 'Типы данных',
  virtualOperations: 'Виртуальные операции',
  operations: 'Операции',
  accounts: 'Аккаунты',
  transfersVesting: 'Переводы и вестинг',
  validators: 'Валидаторы',
  content: 'Контент',
  recovery: 'Восстановление',
  escrow: 'Эскроу',
  committee: 'Комитет',
  invites: 'Инвайты',
  awards: 'Награды',
  subscriptions: 'Подписки',
  accountMarket: 'Рынок аккаунтов',
  proposals: 'Предложения',
  storage: 'Хранилище',
  sharedMemory: 'Разделяемая память',
  blockLog: 'Лог блоков',
  snapshotsStorage: 'Снимки',
  governance: 'Управление',
  chainProperties: 'Параметры цепи',
  stakingDao: 'Стейкинг и DAO',
  committeeGov: 'Комитет',
  apiReference: 'Справочник API',
  jsonRpc: 'JSON-RPC',
  cliWallet: 'CLI-кошелёк',
  clientLibraries: 'Клиентские библиотеки',
  development: 'Разработка',
  buildingDev: 'Сборка',
  testing: 'Тестирование',
  debugging: 'Отладка',
  pluginDevelopment: 'Разработка плагинов',
  advanced: 'Продвинутое',
  security: 'Безопасность',
  databaseSchema: 'Схема базы данных',
  dltArchitecture: 'Архитектура DLT',
  hardforkManagement: 'Управление хардфорками',
};

const zhCN: SidebarLabels = {
  introduction: '简介',
  whatIsViz: '什么是 VIZ Ledger？',
  architecture: '架构',
  keyConcepts: '核心概念',
  runANode: '运行节点',
  gettingStarted: '快速开始',
  configuration: '配置',
  building: '从源码构建',
  docker: 'Docker',
  validatorNode: '验证人节点',
  validatorGuard: '验证人守卫',
  snapshots: '快照',
  monitoring: '监控',
  consensus: '共识',
  fairDpos: 'Fair-DPOS',
  blockProcessing: '区块处理',
  forkResolution: '分叉解决',
  emergencyConsensus: '紧急共识',
  hardforks: '硬分叉',
  p2pNetwork: 'P2P 网络',
  overview: '概览',
  messages: '消息',
  syncScenarios: '同步场景',
  forwardMode: '转发模式',
  statsReference: '统计参考',
  plugins: '插件',
  validator: '验证人',
  snapshot: '快照',
  chain: 'Chain',
  databaseApi: 'Database API',
  webserver: 'Webserver',
  protocol: '协议',
  dataTypes: '数据类型',
  virtualOperations: '虚拟操作',
  operations: '操作',
  accounts: '账户',
  transfersVesting: '转账与质押',
  validators: '验证人',
  content: '内容',
  recovery: '恢复',
  escrow: '托管',
  committee: '委员会',
  invites: '邀请',
  awards: '奖励',
  subscriptions: '订阅',
  accountMarket: '账户市场',
  proposals: '提案',
  storage: '存储',
  sharedMemory: '共享内存',
  blockLog: '区块日志',
  snapshotsStorage: '快照',
  governance: '治理',
  chainProperties: '链参数',
  stakingDao: '质押与 DAO',
  committeeGov: '委员会',
  apiReference: 'API 参考',
  jsonRpc: 'JSON-RPC',
  cliWallet: 'CLI 钱包',
  clientLibraries: '客户端库',
  development: '开发',
  buildingDev: '构建',
  testing: '测试',
  debugging: '调试',
  pluginDevelopment: '插件开发',
  advanced: '高级',
  security: '安全',
  databaseSchema: '数据库架构',
  dltArchitecture: 'DLT 架构',
  hardforkManagement: '硬分叉管理',
};

// ─── Build sidebar from labels ──────────────────────────────────────────────

function buildSidebar(t: SidebarLabels, prefix: string): DefaultTheme.SidebarItem[] {
  const p = (path: string) => `${prefix}${path}`;
  return [
    {
      text: t.introduction,
      items: [
        { text: t.whatIsViz, link: p('/introduction/what-is-viz') },
        { text: t.architecture, link: p('/introduction/architecture') },
        { text: t.keyConcepts, link: p('/introduction/key-concepts') },
      ],
    },
    {
      text: t.runANode,
      items: [
        { text: t.gettingStarted, link: p('/node/getting-started') },
        { text: t.configuration, link: p('/node/configuration') },
        { text: t.building, link: p('/node/building') },
        { text: t.docker, link: p('/node/docker') },
        { text: t.validatorNode, link: p('/node/validator-node') },
        { text: t.validatorGuard, link: p('/node/validator-guard') },
        { text: t.snapshots, link: p('/node/snapshot') },
        { text: t.monitoring, link: p('/node/monitoring') },
      ],
    },
    {
      text: t.consensus,
      items: [
        { text: t.fairDpos, link: p('/consensus/fair-dpos') },
        { text: t.blockProcessing, link: p('/consensus/block-processing') },
        { text: t.forkResolution, link: p('/consensus/fork-resolution') },
        { text: t.emergencyConsensus, link: p('/consensus/emergency-consensus') },
        { text: t.hardforks, link: p('/consensus/hardforks') },
      ],
    },
    {
      text: t.p2pNetwork,
      items: [
        { text: t.overview, link: p('/p2p/overview') },
        { text: t.messages, link: p('/p2p/messages') },
        { text: t.syncScenarios, link: p('/p2p/sync-scenarios') },
        { text: t.forwardMode, link: p('/p2p/forward-mode') },
        { text: t.statsReference, link: p('/p2p/stats-reference') },
      ],
    },
    {
      text: t.plugins,
      items: [
        { text: t.overview, link: p('/plugins/overview') },
        { text: t.validator, link: p('/plugins/validator') },
        { text: t.snapshot, link: p('/plugins/snapshot') },
        { text: t.chain, link: p('/plugins/chain') },
        { text: t.databaseApi, link: p('/plugins/database-api') },
        { text: t.webserver, link: p('/plugins/webserver') },
      ],
    },
    {
      text: t.protocol,
      items: [
        { text: t.dataTypes, link: p('/protocol/data-types') },
        { text: t.virtualOperations, link: p('/protocol/virtual-operations') },
        {
          text: t.operations,
          collapsed: false,
          items: [
            { text: t.overview, link: p('/protocol/operations/overview') },
            { text: t.accounts, link: p('/protocol/operations/accounts') },
            { text: t.transfersVesting, link: p('/protocol/operations/transfers') },
            { text: t.validators, link: p('/protocol/operations/validators') },
            { text: t.content, link: p('/protocol/operations/content') },
            { text: t.recovery, link: p('/protocol/operations/recovery') },
            { text: t.escrow, link: p('/protocol/operations/escrow') },
            { text: t.committee, link: p('/protocol/operations/committee') },
            { text: t.invites, link: p('/protocol/operations/invites') },
            { text: t.awards, link: p('/protocol/operations/awards') },
            { text: t.subscriptions, link: p('/protocol/operations/subscriptions') },
            { text: t.accountMarket, link: p('/protocol/operations/account-market') },
            { text: t.proposals, link: p('/protocol/operations/proposals') },
          ],
        },
      ],
    },
    {
      text: t.storage,
      items: [
        { text: t.sharedMemory, link: p('/storage/shared-memory') },
        { text: t.blockLog, link: p('/storage/block-log') },
        { text: t.snapshotsStorage, link: p('/storage/snapshots') },
      ],
    },
    {
      text: t.governance,
      items: [
        { text: t.chainProperties, link: p('/governance/chain-properties') },
        { text: t.stakingDao, link: p('/governance/staking-and-dao') },
        { text: t.committeeGov, link: p('/governance/committee') },
      ],
    },
    {
      text: t.apiReference,
      items: [
        { text: t.jsonRpc, link: p('/api/json-rpc') },
        { text: t.cliWallet, link: p('/api/cli-wallet') },
        { text: t.clientLibraries, link: p('/api/client-libraries') },
      ],
    },
    {
      text: t.development,
      items: [
        { text: t.buildingDev, link: p('/development/building') },
        { text: t.testing, link: p('/development/testing') },
        { text: t.debugging, link: p('/development/debugging') },
        { text: t.pluginDevelopment, link: p('/development/plugin-development') },
      ],
    },
    {
      text: t.advanced,
      items: [
        { text: t.security, link: p('/advanced/security') },
        { text: t.databaseSchema, link: p('/advanced/database-schema') },
        { text: t.dltArchitecture, link: p('/advanced/dlt-architecture') },
        { text: t.hardforkManagement, link: p('/advanced/hardfork-management') },
      ],
    },
  ];
}

// ─── Nav builder ────────────────────────────────────────────────────────────

function localizedNav(
  prefix: string,
  introLabel: string,
  nodeLabel: string,
  protocolLabel: string,
  apiLabel: string,
): DefaultTheme.NavItem[] {
  const p = (path: string) => `${prefix}${path}`;
  return [
    { text: introLabel, link: p('/introduction/what-is-viz') },
    { text: nodeLabel, link: p('/node/getting-started') },
    { text: protocolLabel, link: p('/protocol/data-types') },
    { text: apiLabel, link: p('/api/json-rpc') },
    { text: 'GitHub', link: githubRepo },
  ];
}

// ─── Config ─────────────────────────────────────────────────────────────────

export default defineConfig({
  title: 'VIZ Ledger Documentation',
  description: 'Official documentation for VIZ Ledger — hybrid DLT with Fair-DPOS consensus',
  base: '/viz-cpp-node/',
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
        sidebar: buildSidebar(en, ''),
      },
    },
    ru: {
      label: 'Русский',
      lang: 'ru',
      themeConfig: {
        nav: localizedNav('/ru', 'Введение', 'Запуск узла', 'Протокол', 'API'),
        sidebar: buildSidebar(ru, '/ru'),
      },
    },
    'zh-CN': {
      label: '中文',
      lang: 'zh-CN',
      themeConfig: {
        nav: localizedNav('/zh-CN', '简介', '运行节点', '协议', 'API'),
        sidebar: buildSidebar(zhCN, '/zh-CN'),
      },
    },
  },
});
