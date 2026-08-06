# 验证者运维手册

从一台全新的 Debian VPS 到一个正在生产区块的 VIZ 验证者，以及如何让它持续运行。

本页是端到端的操作流程。参考资料不再重述，而是直接链接：
[配置](../node/configuration.md) 列出所有选项，
[验证者节点](../node/validator-node.md) 讲生产循环与日志消息，
[构建](../node/building.md) 讲从源码构建。

---

## 第 0 部分 — 开始之前

### 两种节点画像

| 画像 | 是否签名区块 | 公网暴露面 | 用途 |
|---|---|---|---|
| **验证者** | 是 | 无 —— RPC 仅监听回环，p2p 仅出站 | 生产区块 |
| **中继 / 种子** | 否 | `:2001` 入站，可选公开 RPC | 服务网络，为你自己的验证者提供可靠对等节点 |

把两者分开。验证者持有签名密钥；把它放到公网 p2p 暴露面上没有任何收益，代价却是
失去密钥的隔离性。中继画像见第 6 部分。

### 前置条件

- 一台 Debian 12 或 13 的 VPS，具备 root 或 sudo 权限。
- 一个已注册的 VIZ 账户。如何获得账户，以及成为可行验证者所需的权益，超出本页范围
  —— 参见[质押与 DAO](../governance/staking-and-dao.md)。
- **不**需要域名。验证者不需要入站 DNS。

### 规格

验证者是一台小机器。它不保存历史、不提供 API、不存归档 —— 下面的配置把它的整体
占用限制住了。

| | 验证者 | 无密钥中继 |
|---|---|---|
| vCPU | 2 | 2（若接受大量入站对等节点则 4） |
| 内存 | 4 GB + 2 GB swap | 4 GB + 2 GB swap |
| 磁盘 | 20 GB SSD | 20 GB SSD（若对外提供快照则 40 GB） |

磁盘用在哪里 —— 这样你可以自己核对数字，而不是相信它：

| | 大小 | 由什么限制 |
|---|---|---|
| `shared_memory.bin` | 2 GB，按 2 GB 步进增长 | `shared-file-size` / `inc-shared-file-size` |
| DLT 滚动区块日志 | 约 3.5 天的区块 | `dlt-block-log-max-blocks = 100000` |
| 本地快照 | 2 个文件；每个小于 2 GB | 数量由 `snapshot-every-n-blocks` + `snapshot-max-age-days` 决定。2 GB 是 P2P 传输上限，因此它是天花板 —— 真实的主网快照更小。在按最坏情况规划前，先用 `du -sh` 看看你自己的。 |
| Docker 日志 | 30 MB | `compose.yml` 中的 `max-size` × `max-file` |

其余就是 Debian 和 `vizd` 镜像。验证者上没有任何东西会脱离你设定的上限增长，这就是
20 GB 是个真实数字而不是一厢情愿的原因 —— 但请看第 2 部分关于快照保留的警告，那是
唯一一个可能悄悄打破这个结论的设置。

不要削减内存。签名发生在 3 秒的时隙内；生产期间发生换页的节点会错过时隙并报告
`lag`。那 2 GB swap 是给快照导入期间内存尖峰用的安全阀，不是内存的替代品。

两种画像都需要这块 swap —— 中继同样会导入快照，因此承受同样的尖峰。区别只在于换页的
代价：中继换页会变慢，而验证者换页会漏掉一个区块。

中继的 CPU 随它接受的入站对等节点数量扩展，而不是随「公开」这件事本身 —— 它在
`:2001` 上本来就是公开的。用 `p2p-max-connections` 设上限，再据此定规格：默认值很
宽松，而一个为你自己的验证者供给的中继并不需要承载 200 个陌生人。

公开 RPC 是另一台机器、另一套经济账 —— 历史插件会存下所有东西。那个画像请看
[Docker](../node/docker.md) 里的规格表，并且不要把它放在你的验证者上。

### 端口

| 端口 | 验证者 | 中继 | 用途 |
|---|---|---|---|
| 2001 | 仅出站 | **入站** | p2p |
| 8090 | 仅回环 | 可选公开 | HTTP JSON-RPC |
| 8092 | 仅出站 | 可选入站 | 快照传输 |

---

## 第 1 部分 — 主机准备

这里的所有命令都在一台全新机器上以 root 身份按顺序执行。最后有一道在部署节点前
必须通过的关卡。

### 一个带 SSH 密钥的 sudo 用户

在你的**工作站**上，如果还没有密钥：

```bash
ssh-keygen -t ed25519 -C "viz-validator"
```

在**服务器**上，以 root 身份：

```bash
adduser --gecos "" viz
usermod -aG sudo viz
install -d -m 700 -o viz -g viz /home/viz/.ssh
```

然后从工作站推送公钥：

```bash
ssh-copy-id -i ~/.ssh/id_ed25519.pub viz@YOUR_SERVER_IP
```

继续之前先确认它可用 —— `ssh viz@YOUR_SERVER_IP` 必须在**不提示输入密码**的情况下
成功。

### SSH 加固

```bash
sudo tee /etc/ssh/sshd_config.d/10-hardening.conf >/dev/null <<'EOF'
PermitRootLogin no
PasswordAuthentication no
KbdInteractiveAuthentication no
EOF
sudo sshd -t && sudo systemctl reload ssh
```

::: danger 保持当前会话不要关闭
不要关掉这个 shell。另开**第二个**终端，执行 `ssh viz@YOUR_SERVER_IP`，确认能登录
成功。之后再关闭第一个。如果密钥认证坏了而你已经登出，你就被锁在门外，恢复手段只剩
服务商的救援控制台。
:::

### 防火墙

入站默认拒绝。除 SSH 外，验证者不需要任何入站端口 —— p2p 只出站，RPC 由 Docker
发布在回环地址上。

```bash
sudo apt update && sudo apt install -y ufw
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow OpenSSH
sudo ufw --force enable
```

中继运维者需要加上 `sudo ufw allow 2001/tcp` —— 见第 6 部分。验证者上不要加。

### 时间同步

**这一步不是可选的。** 区块生产按 3 秒间隔的时隙调度。时钟偏差超过一个时隙的一小部
分，就会签出网络拒绝的区块，或者干脆错过自己的时隙，而症状看起来和网络故障一模一样。

```bash
sudo apt install -y systemd-timesyncd
sudo timedatectl set-ntp true
timedatectl status
```

节点自身也会做 NTP 检查，并在偏差过大时拒绝生产 —— 见
[验证者节点](../node/validator-node.md)的 NTP 一节。主机同步和节点检查是互补关系，
不是二选一。

### Swap

2 GB 足够。这里的 swap 是给快照导入期间的瞬时尖峰准备的安全阀，而不是你打算长期占用
的余量 —— 长期泡在 swap 里的验证者会漏时隙。

```bash
sudo fallocate -l 2G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

持久化一个较低的 swappiness，让内核优先回收页缓存而不是把活跃的链状态换出：

```bash
echo 'vm.swappiness=10' | sudo tee /etc/sysctl.d/99-vizd.conf
sudo sysctl --system
```

### Docker

用 Docker 官方 apt 仓库 —— Debian 打包的 `docker.io` 版本落后，而且不含 compose
插件。

```bash
sudo apt install -y ca-certificates curl
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/debian/gpg \
  -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
https://download.docker.com/linux/debian $(. /etc/os-release && echo "$VERSION_CODENAME") stable" \
  | sudo tee /etc/apt/sources.list.d/docker.list >/dev/null
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io \
  docker-buildx-plugin docker-compose-plugin
sudo usermod -aG docker viz
```

登出再登入，让用户组变更生效。

**关卡 —— 三项全部通过后才继续：**

```bash
timedatectl | grep 'System clock synchronized: yes'
docker run --rm hello-world
sudo ufw status | grep 'Status: active'
```

---

## 第 2 部分 — 部署节点

### 生成签名密钥

```bash
docker run --rm -it vizblockchain/vizd:latest cli_wallet --suggest-brain-key
```

记录 WIF 格式的私钥和 `VIZ...` 公钥。私钥写进下面的 `config.ini`；公钥在第 3 部分
注册到链上。

**你账户的活动密钥（active key）永远不要出现在这台机器上。** 它留在浏览器里，供第 3
部分的切换使用。签名密钥是一个独立、可丢弃的身份 —— 如果泄露，你轮换它（第 5
部分）即可，不会损失其他任何东西。

### 目录布局

```bash
sudo install -d -o viz -g viz /opt/vizd/logs
cd /opt/vizd
```

这里放三样东西：`compose.yml`、`config.ini` 和 `logs/`。链状态**不在**这里 ——
它在一个 Docker 具名卷里，这正是第 5 部分那张决策表的关键所在。

### `compose.yml`

```yaml
name: viz-validator

services:
  vizd:
    image: vizblockchain/vizd:latest
    container_name: viz-validator
    restart: unless-stopped
    ports:
      - "127.0.0.1:8090:8090"   # 仅回环 —— 用于健康检查，绝不公开
    volumes:
      - vizd-state:/var/lib/vizd
      - ./config.ini:/etc/vizd/config.ini:ro
      - ./logs:/var/log/vizd
    logging:
      driver: json-file
      options:
        max-size: "10m"
        max-file: "3"

volumes:
  vizd-state:
```

这个文件里有三处是刻意为之的：

- **配置以 `:ro` 挂载是正确的。** 入口脚本会在使用前把 `/etc/vizd/config.ini`
  复制到数据目录，因此容器从不写入被挂载的文件。
- **链状态存放在具名卷 `vizd-state` 中。** `docker compose down` 会保留它；
  `docker compose down -v` 会销毁它。这一个参数就是「重启」和「彻底重新引导」之间的
  区别。
- **日志走 Docker 的 `json-file` 驱动**，并设了上限使其无法撑满磁盘 —— 用
  `docker compose logs` 查看。`./logs` 挂载是留给文件 appender 的（如果你要加）；
  在下面这套只写 stderr 的日志配置下，它会一直是空的。

### `config.ini`

```ini
# ─── p2p（仅出站；没有任何东西对外监听） ─────────────────────────────
p2p-seed-node = seed3.viz.world:2001
p2p-seed-node = seed1.viz.world:2001
p2p-seed-node = rpc.viz.cx:2001

# ─── RPC —— 由 compose 发布在回环地址，供健康检查使用 ────────────────
webserver-http-endpoint = 0.0.0.0:8090
webserver-thread-pool-size = 1
single-write-thread = true
enable-plugins-on-push-transaction = false

# ─── 共享内存 ────────────────────────────────────────────────────────
shared-file-size = 2G
min-free-shared-file-size = 500M
inc-shared-file-size = 2G

# ─── 插件：只要生产者 + 健康检查 RPC ─────────────────────────────────
plugin = chain p2p json_rpc webserver database_api validator validator_api
plugin = snapshot

# ─── 验证者身份 ──────────────────────────────────────────────────────
validator = "YOUR_ACCOUNT"
private-key = YOUR_SIGNING_WIF

# ─── 快照：从可信对等节点引导，绝不对外提供 ──────────────────────────
snapshot-dir = /var/lib/vizd/snapshots
sync-snapshot-from-trusted-peer = true
trusted-snapshot-peer = seed3.viz.world:8092
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
trusted-snapshot-peer = rpc.viz.cx:8092
allow-snapshot-serving = false
snapshot-every-n-blocks = 28800
snapshot-max-age-days = 2
dlt-block-log-max-blocks = 100000

# ─── 生产安全 ────────────────────────────────────────────────────────
required-participation = 33
enable-stale-production = false
skip-virtual-ops = true
clear-votes-before-block = 0

# ─── 日志 ────────────────────────────────────────────────────────────
# fc 在这里很严格：[log.*]/[logger.*] 段内不允许行尾注释，
# 且 level 必须是 all/debug/info/warn/error/off 之一。"none" 是无效值，
# 会静默回退到默认的 stderr 日志。
[log.console_appender.stderr]
stream=std_error
[logger.default]
level=info
appenders=stderr
```

两个占位符：

- `validator = "YOUR_ACCOUNT"` —— 你的账户名，**要加引号**。
- `private-key = YOUR_SIGNING_WIF` —— 上面密钥对里的裸 WIF，**不要加引号**。

选项名是 `validator`。`witness` 作为已弃用的别名仍然可用，但会输出
`Config option 'witness' is deprecated, use 'validator' instead.` —— 如果你看到这条
警告，说明你复制了旧的片段。

`seed2.viz.world` 出现在快照列表里，但**不在** p2p 列表里。这个不对称是刻意的：它的
`:8092` 提供快照，它的 `:2001` 不接受对等连接。不要去「修正」它。

::: warning 快照保留只按时间，没有「保留 N 份」的上限
磁盘上的快照数 = 生成频率 × 保留时长，除此之外没有任何限制。
`snapshot-every-n-blocks = 28800` 大约是每天一份（28800 × 3 秒），因此配合
`snapshot-max-age-days = 2` 你大约会持有两个文件。把频率降到 `1200`，你就是在要求
约 240 个每个最多 2 GB 的文件 —— 早在按时间清理生效之前，它就会撑满本手册规格表里
的任何磁盘。

验证者不需要很深的本地快照历史。两个文件足够支撑一次快速的本地重启；比这更糟的情况
本来就是从可信对等节点重新引导，而那只需要几分钟（见第 5 部分「机器丢失」）。
:::

### 启动

这个文件里有你的签名密钥，所以在节点第一次运行之前就把它锁好：

```bash
chmod 600 config.ini
docker compose up -d
docker compose logs -f
```

在没有状态的首次启动时，你会看到：

```
Node has no state. Triggering P2P snapshot sync from trusted peers...
```

从快照引导只需**几分钟，而不是几小时**。如果你看到的是节点从创世块开始重放区块日志，
说明它没能连上可信快照节点 —— 检查 `trusted-snapshot-peer` 列表和出站 `:8092`。

**关卡：**

```bash
# 相隔约 7 秒的两次调用之间，头部高度必须上升
curl -s http://127.0.0.1:8090 -H 'Content-Type: application/json' \
  --data '{"id":1,"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]]}' \
  | grep -o '"head_block_number":[0-9]*'

# 不允许有任何东西对外监听
ss -tlnp | grep 8090   # 必须只显示 127.0.0.1:8090，别无其他
```

---

## 第 3 部分 — 链上切换

::: warning 停
在第 2 部分的关卡通过**并且**节点已追上网络头部之前，不要开始这一部分。在仍在同步的
节点上注册签名密钥，等于让调度器给你安排你填不上的时隙。
:::

### 1. 设置签名密钥

打开 `https://wallet.viz.world/dao/witness-params/`，**在浏览器里**用账户的**活动
密钥**登录。把签名密钥设置为你在第 2 部分生成的 `VIZ...` 公钥。

::: warning 先设密钥，再拿票
没有密钥的验证者会报告 `running_version 0.0.0`，并且根本不会出现在网页钱包的 DAO
验证者列表里 —— 也就无法被投票。正是设置签名密钥让它可见。顺序颠倒过来做，看起来就
像钱包坏了。
:::

### 2. 获得投票

零票的验证者虽然已注册，但**永远不会被调度** —— 它的 `virtual_scheduled_time` 是
max-uint，调度器永远轮不到它。它会无限期地停在链头上，健康、空闲，什么也不生产。

票来自 `account_validator_vote` 操作，并按投票者的质押权益加权，因此少数大持有者的
权重超过一群小散户。参见[质押与 DAO](../governance/staking-and-dao.md)。

### 3. 验证生产

```bash
curl -s http://127.0.0.1:8090 -H 'Content-Type: application/json' \
  --data '{"id":1,"jsonrpc":"2.0","method":"call","params":["validator_api","get_validator_by_account",["YOUR_ACCOUNT"]]}'
```

确认 `signing_key` 与你的公钥一致，并且 `last_confirmed_block_num` 在两次调用之间
增长。

::: warning 一旦被投票并开始生产，节点就必须保持在线
连续错过足够多的被调度区块，会导致 VIZ 通过把签名密钥置空来自动停用该验证者。恢复
意味着重新注册密钥并重新赢回调度位置 —— 对正在生产的验证者做计划内维护，应该是一次
短暂重启，而不是一个下午。
:::

::: tip total_missed 是终身计数器
它永不重置。**现在**就记下你的基线，并对增量告警。盯着绝对值除了说明节点存在过，
什么也说明不了。
:::

要停用或回滚一个验证者，把它的签名密钥设为空密钥：

```
VIZ1111111111111111111111111111111114T1Anm
```

这正是链自动停用你时所用的机制，所以它是干净的停止，而不是取巧。

::: tip cli_wallet 的 TLS
截至 2026-07-27，`vizd` 镜像里的 `cli_wallet` 会让每一次 `wss` 握手失败，拒绝普通
`http`，并且只讲普通 `ws` —— 而公共节点并不暴露它。如果你碰到这个问题，请改用网页
钱包或客户端库来广播。在假定它依然如此之前，请重新测试。
:::

**关卡：** `signing_key` 等于你的公钥，且相隔一分钟的两次调用之间
`last_confirmed_block_num` 有增长。

---

## 第 4 部分 — 健康监控

有三件事会悄无声息地出错：容器死掉、链头停止前进、或者节点跟得上网络但错过自己的
时隙。一个检查覆盖这三种情况，并且只在失败时告警 —— 成功时也来打扰你的监控最终会被
静音，而被静音的监控就不是监控。

```bash
#!/usr/bin/env bash
# /opt/vizd/health.sh —— 由 cron 每 15 分钟执行一次。
# 检查容器是否在运行、链头是否在前进、错过的区块数是否在增长。
# 只在失败时告警；不发成功刷屏。
set -euo pipefail

DIR=/opt/vizd
RPC=http://127.0.0.1:8090
STATE="$DIR/health.state"

source "$DIR/.env"   # ALERT_TOKEN, ALERT_CHAT, WITNESS_ACCOUNT

alert() {
    curl -s --max-time 10 "https://api.telegram.org/bot${ALERT_TOKEN}/sendMessage" \
        -d chat_id="${ALERT_CHAT}" \
        --data-urlencode text="viz-validator ($(hostname)): $1" >/dev/null || true
}

if ! docker ps --format '{{.Names}}' | grep -q '^viz-validator$'; then
    alert "container not running"; exit 1
fi

rpc() { curl -s --max-time 10 "$RPC" -H 'Content-Type: application/json' --data "$1"; }
DGP='{"id":1,"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]]}'

head1=$(rpc "$DGP" | grep -o '"head_block_number":[0-9]*' | grep -o '[0-9]*$' || true)
sleep 9   # 三个区块间隔
head2=$(rpc "$DGP" | grep -o '"head_block_number":[0-9]*' | grep -o '[0-9]*$' || true)

if [ -z "$head1" ] || [ -z "$head2" ]; then
    alert "RPC not answering on 127.0.0.1:8090"; exit 1
fi
if [ "$head2" -le "$head1" ]; then
    alert "head frozen at #$head2 (was #$head1 nine seconds earlier)"; exit 1
fi

missed=$(rpc "{\"id\":2,\"jsonrpc\":\"2.0\",\"method\":\"call\",\"params\":[\"validator_api\",\"get_validator_by_account\",[\"$WITNESS_ACCOUNT\"]]}" \
    | grep -o '"total_missed":[0-9]*' | grep -o '[0-9]*$' || true)
if [ -n "$missed" ]; then
    prev=$(cat "$STATE" 2>/dev/null || echo "$missed")
    [ "$missed" -gt "$prev" ] && alert "missed blocks growing: $prev -> $missed"
    echo "$missed" > "$STATE"
fi
```

解析用 `curl` 加 `grep` 是故意的。不用 `jq`，所以最小化的机器上不需要额外安装任何
东西，检查本身也没有会过期的依赖。

凭据放在旁边一个权限 600 的 `.env` 里：

```bash
cat > /opt/vizd/.env <<'EOF'
ALERT_TOKEN=123456:your-telegram-bot-token
ALERT_CHAT=your-telegram-chat-id
WITNESS_ACCOUNT=your-account
EOF
chmod 600 /opt/vizd/.env
chmod 700 /opt/vizd/health.sh
```

以及 cron 条目（以 `viz` 用户执行 `crontab -e`）：

```text
*/15 * * * * /opt/vizd/health.sh >> /opt/vizd/health.log 2>&1
```

节点自身在每次生产尝试时会记录什么 —— 已生产、已错过、少数派分叉、看门狗 ——
见[验证者节点](../node/validator-node.md)，那里把每种结果都列成了表，本页不再重复。
关于指标和更深入的告警，见[监控](../node/monitoring.md)。

**关卡 —— 去测试告警，不要假设它有效：**

```bash
docker compose stop vizd     # 然后等下一次 cron 触发
# 15 分钟内必须收到一条告警
docker compose start vizd
```

未经测试的警报不是警报。趁现在漏几个时隙毫无代价的时候做一次，而不是在事故中才发现
token 是错的。

---

## 第 5 部分 — 日常运维

从这里开始。这张表是运维者最常选错的地方，而选错的代价是本来用 `restart` 就不会丢的
时隙。

| 情况 | 命令 | 代价 |
|---|---|---|
| 仅修改配置 | `docker compose restart vizd` | 约 1 个时隙 |
| 新镜像 | 先 `docker compose pull`，**再** `docker compose up -d` | 补齐缺口的重放，停机接近于零 |
| 卡死或处于坏分叉上 | `docker compose down -v && docker compose up -d` | 完全重新引导；重新同步完成前会持续漏时隙 |

### 镜像升级

`vizblockchain/vizd:latest` 在每次推送到 master 时都会重新构建，因此没有版本号标签
需要你去改 —— 标签本身在移动。

陷阱：**当标签在本地已存在时，`docker compose up -d` 不会去拉取更新的 `:latest`。**
它看到标签匹配，就复用本地镜像，并报告成功。你必须先执行 `docker compose pull`，
否则你只是重启了同一个二进制，然后得出「升级没起作用」的结论。

```bash
cd /opt/vizd
docker compose pull
docker compose up -d
docker compose logs -f
```

**一台一台地升级**。在两台之间，确认链头在前进且 `total_missed` 没有增长。如果这次
拉取带来了回归，上一个镜像还在磁盘上 —— `docker images vizblockchain/vizd` 会按 id
列出它，你可以在 `compose.yml` 里固定那个 id 来回滚。

### 重建容器不会清掉卡死状态

链状态存放在 `vizd-state` 卷里，而 compose 在 `up -d`、`restart` 甚至
`--force-recreate` 时都会重新挂载这个卷。因此新容器会重新加载*同一份*坏状态并再次
卡死 —— 这读起来像「重启没用」，而实际上什么都没被重置。

只有 `docker compose down -v` 会丢掉这个卷。这就是全部差别，也正因如此这个参数值得
背下来，而不是在事故中现查。

### 死分叉恢复，以及它的顺序陷阱

症状：对等节点已连接但链头冻结，或者某个规范节点记录说因为死分叉而软封禁了你。

**在清空之前先配置好规范快照来源。** 如果你先 `down -v`，节点会从手上现有的对等节点
重新同步 —— 而在死分叉的情况下，那些可能正是把死分叉喂给你的同一批节点。你付出了
完整重新引导的代价，然后回到原点。顺序：

1. 确认 `config.ini` 的 `trusted-snapshot-peer` 列表以 `seed3.viz.world:8092`
   开头，且 `sync-snapshot-from-trusted-peer = true`。
2. 确认 `p2p-seed-node` 列表以 `seed3.viz.world:2001` 开头。对等节点是按顺序尝试
   的，所以规范链头必须放在第一位。
3. `docker compose down -v && docker compose up -d`。
4. 留意 `Node has no state. Triggering P2P snapshot sync from trusted peers...`，
   并确认链头落在网络头部附近，而不是原先冻结的高度。

### 升级后状态被拒绝

如果节点在启动时退出，并抱怨状态是由不同的编译器、构建或 Boost 版本生成的，那不是
损坏 —— 那是刻意的拒绝。见[升级到 Boost 1.9x](./boost-1.9x-upgrade.md)。

### 密钥轮换

1. 完全按第 2 部分的方式生成一对新密钥。
2. 更新 `config.ini` 里的 `private-key`。
3. `docker compose restart vizd`。
4. 完全按第 3 部分的方式，把新的**公**钥重新注册到链上。

链上注册就是切换的分界线。在它生效之前，旧密钥生产有效区块；它一生效，就只有新密钥
有效。不存在两者同时有效的窗口，因此没有双重生产的风险 —— 但在第 3 步和第 4 步之间
**确实**有一个窗口：此时节点持有一个链尚未接受的密钥，并会报告 `no_private_key`。
把这个窗口压短。

### 机器丢失

没有什么需要恢复。你的身份是签名密钥加上它的链上注册，二者都不在这台机器上 ——
密钥在你的密码管理器里，注册在链上。在新硬件上重跑第 1–2 部分，节点会在几分钟内从
快照重新引导。不要备份链状态；相比网络会交给你的东西，它一文不值。

### 运行多个验证者

一台机器一个签名密钥。**绝不要把同一个 WIF 用在两台机器上** —— 那就是双重生产：两个
节点为同一个时隙签出不同的区块，而这正是网络被设计来惩罚的行为。使用不同密钥的不同
账户之间完全独立，这样做完全没问题。

---

## 第 6 部分 — 无密钥的中继 / 种子画像

VIZ 缺少公共种子节点。中继在签名风险上不花你任何代价，同时在你自己掌控的基础设施上
为你的验证者提供一个可靠对等节点，而验证者本身仍然对入站关闭。如果你运行着一个
验证者，那么运行一个中继就是你能增加的最具价值的第二台机器。

中继就是第 2 部分的部署，加上这些差异：

- **没有 `validator`，也没有 `private-key`。** 它不签任何东西，所以机器上没有需要
  保护的密钥。
- **`p2p-endpoint = 0.0.0.0:2001`**，再加 `sudo ufw allow 2001/tcp`。这是唯一一个
  入站 p2p 属于正确做法的地方。
- 如果你想给其他运维者提供快照，则设 **`allow-snapshot-serving = true`**；同时加上
  `sudo ufw allow 8092/tcp`。这是唯一会改变磁盘数字的变更 —— 你现在要为别人抓取而
  保留快照，所以按 40 GB 规划，并留意 `snapshot-max-age-days`。
- **只有在你要服务 API 客户端时，才添加 `network_broadcast_api` 和历史插件。** 它们
  消耗内存和磁盘，正是它们把一台 20 GB 的机器变成 50 GB 以上的机器。纯中继两者都不
  需要。
- **公开 RPC（如果有）必须放在带 TLS 的反向代理后面。** 绝不要直接暴露 `:8090`。

::: warning 不要把中继和你唯一的验证者放在同一台机器上
那样一台机器故障就会同时干掉你的生产者和它依赖的对等节点。曾有一起分叉事故牵涉到
在同一主机上给验证者供给的中继。如果两者都运行，请放在不同机器上。
:::

---

## 第 7 部分 — 症状表

这里是主机与部署层面的症状。在节点自身生产日志中可见的症状 —— `no_private_key`、
`low_participation`、`minority_fork` 之类 —— 见
[验证者节点](../node/validator-node.md)，那里把每种结果都列成了表。

| 症状 | 首选命令 | 常见原因 |
|---|---|---|
| 容器反复重启 | `docker compose logs --tail=100 vizd` | `config.ini` 格式错误 —— `[log.*]` 段内有行尾注释，或日志级别无效 |
| 回环地址上的 RPC 无响应 | 先 `docker compose ps`，再 `ss -tlnp \| grep 8090` | 容器没起来，或 `ports:` 里少了 `127.0.0.1:` 前缀 |
| 链头冻结，对等节点已连接 | `docker compose logs --tail=200 vizd \| grep -i fork` | 状态卡死或处于死分叉 —— 见第 5 部分；注意普通重启不会清掉它 |
| 链头冻结，零对等节点 | `docker compose logs vizd \| grep -i 'p2p\|peer'` | 所有 `p2p-seed-node` 都不可达，或出站 `:2001` 被拦 —— 用 `nc -z seed3.viz.world 2001` 测试 |
| `total_missed` 上升，链头在前进 | `timedatectl` | 时钟偏差，或机器太慢无法在时隙内签名 —— 检查负载和 swap 压力 |
| 切换看起来正确但什么都没生产 | `get_validator_by_account` —— 比对 `signing_key`，读 `virtual_scheduled_time` | 零票因此永不被调度（第 3 部分），或注册的密钥与 `config.ini` 不一致 |
| 升级后启动时状态被拒绝 | `docker compose logs vizd \| head -40` | Boost/编译器状态版本不匹配 —— [升级到 Boost 1.9x](./boost-1.9x-upgrade.md) |
| 磁盘满 | `df -h && docker system df` | 旧镜像和悬空卷 —— `docker image prune -a`；检查快照保留（`snapshot-max-age-days`）以及是否设置了 `logging.options.max-size` |
