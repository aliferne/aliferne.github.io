---
title: gitea & cadddy 的简易配置指南
date: 2026-10-29 11:00:00
categories:
- linux
tags:
- gitea
- caddy
---

# 前言

这只是一篇非常简单的部署文章，文章内容可能基本上只适用于我这个环境。

先说背景，大约几个月前，我从海鲜市场那里购置了一个迷你主机，之后就一直想把它用起来，偶然了解到了 easytier 这个东西之后，我解决了远程连接的问题 (p2p)，但是之后就没怎么动过这台电脑了，现在就单纯想着干脆搞个 Git 自托管服务吧，参考了 AI 建议和一些博客/文档之后，决定搞个 Gitea + Caddy 的服务模式。

下文以本机指代我在使用的电脑，主机指代服务器（放在家里）。

# hosts 文件修改

这里涉及到一些隐私问题，因此 IP 地址等经过一定的修改，而且我没什么运维经验，请各位不要想着攻击我的肉鸡，真的不耐🌿的(TAT)

我的主机 P2P 虚拟 IP 是 20.132.132.1 (下称 PIP)，但是每次都要输入这玩意其实挺烦的，因此就想着能不能把它映射成域名。

我们都知道客户端访问网站，会经过 DNS 解析把域名解析为实际的 IP 地址之后再去访问对应的服务器，然后服务器网关再根据过来的 IP 和载荷决定要转给哪个端口上运行的服务去处理。前者是 DNS 解析，后者则是反向代理，这里只简要介绍这么多。

我们现在需要处理的是 DNS 解析部分，当然我们的主机并不跑在公网上，因此不需要（也改不了）公共域名解析服务，本机配置文件有个叫做 `/etc/hosts` 的，那个实际上就是本机本地的 DNS 解析服务（当然也有 dnsmasq，但我们只是需要一个最简单的方法）。

打开这个文件，加入：

```
20.132.132.1 ferneserver.net
```

然后保存，接下来 `ping ferneserver.net`，发现可以 ping 通过，
然后 `ssh ferne@ferneserver.net` 并输入密码验证：

```
> ssh ferne@ferneserver.net
# 省略一堆内容
Last login: Wed Oct 28 23:03:13 2026 from 20.132.132.2
Welcome to fish, the friendly interactive shell
Type help for instructions on how to use fish
ferne@server ~>
```

可以看到登录进来了，也就是说 DNS 解析这一关我们搞定了，不过注意，此时 `curl -Ivv http://ferneserver.net/` 是没有用的（连不到主机），原因在于我们还没配置反代，现在只有 ssh 在跑， 因此这里实际上只是通过 DNS 解析访问了主机的 ssh 服务。

# Gitea 配置

这里基本上就是按照 [文档][gitea docs - deployment] 在配置，我们讲一些命令内容。

一开始会让你创建一个叫 git 的用户，这一步注意 `--disable-password` 加进去之后就没办法 `su - git` 去登录了，我们要以 git 用户跑对应命令的话，只能用 `sudo -u git <cmd>`，这样子就会要求验证当前用户的密码而不是 git 这个用户的密码了。


创建完工作路径并将 gitea 复制到全局位置之后运行：

```bash
sudo -u git GITEA_WORK_DIR=/var/lib/gitea/ /usr/local/bin/gitea web --config /etc/gitea/app.ini
```

接下来会出现一个默认监听 localhost:3000 的网页，此时我们在本机浏览器只要输入 `http://ferneserver.net:3000` 就能访问这个配置页面，然后按照提示配置好就可以了。

配置完成之后再修改一些文件的权限：

```bash
chmod 750 /etc/gitea
chmod 640 /etc/gitea/app.ini
```

现在我们需要配置 systemd 服务，这一步就相当简单了，打开 [参考配置][gitea systemd service] 并复制粘贴内容到新创建的 `/etc/systemd/system/gitea.service` 中即可，最后运行 `sudo systemctl start --now gitea` 即可。


# Caddy 配置

注意以下内容我用的浏览器都是 Firefox, Edge 很喜欢在明确不要 https 的网站上加 https，导致我在 Edge 上经常打不开服务，都不知道服务通没通……

Caddy 的介绍可以看 [文档][caddy docs]，我们需要的是它的反代功能。

简单参考一下教程的快速入门之后，我们直接来写 Caddyfile，由于本机已经配置好了 DNS
解析服务，我们直接写入：

```caddyfile
http://ferneserver.net {
    root * /var/www/html
    file_server {
        index index.html
    }
}

http://git.ferneserver.net {
    reverse_proxy :3000
}
```

`http://` 用于显式声明我们不需要 caddy 默认使用的 https 模式，由于我们没有 SSL 证书，验证肯定是不需要的（反正就自己用）。

第一行表示访问 `http://ferneserver.net` 时将站点的根目录设置到 `/var/www/html` 下，并对外提供根目录下的 `index.html` 这个文件（这是默认的，我只是写明了），这个 html 文件的内容随便写就行。

第二行是我们的反代，当访问 `http://git.ferneserver.net` 时反代到 `localhost:3000` 端口，以对外提供 gitea 服务。

最后在主机上运行 `sudo caddy run --config Caddyfile --watch`

这样之后使用 `curl -I` 分别测试这两个地址：

```
> curl -I ferneserver.net
HTTP/1.1 200 OK
Accept-Ranges: bytes
Content-Length: 18753
Content-Type: text/html; charset=utf-8
Etag: "dluyi1aldhc0-egx"
Last-Modified: Sat, 03 Oct 2026 05:33:27 GMT
Server: Caddy
Vary: Accept-Encoding
Date: Thu, 29 Oct 2026 02:25:45 GMT
```

然后看看 git 子域名的服务：

```
> curl -I git.ferneserver.net
HTTP/1.1 200 OK
Accept-Ranges: bytes
Content-Length: 18753
Content-Type: text/html; charset=utf-8
Etag: "dluyi1aldhc0-egx"
Last-Modified: Sat, 03 Oct 2026 05:33:27 GMT
Server: Caddy
Vary: Accept-Encoding
Date: Thu, 29 Oct 2026 02:26:07 GMT
```

状态码都是 200，服务也是 caddy 在提供，这就没什么问题了。 同时我们接下来要启动 systemd 服务，这部分看 [文档][caddy systemd service] 就行，默认情况下是读取 `/etc/caddy/Caddyfile` 的配置文件，因此需要 `sudo ln` 过去（注意这里如果配置文件在 `/home` 目录下则不能创建软链接，原因下面讲），或者直接 `cp` 过去也大差不差。

启动服务之后同理，再用 `curl -I` 测试，能通，那么就都结束了。

需要注意， **当使用 systemd 启动 caddy 时， caddy 对 `/home` 目录下的所有文件都是无法访问的** ，请确保复制到别的地方/创建硬链接到别的地方，否则可能会 404

然后打开浏览器看一下，发现也是 OK 的：

根服务：

![access-root][access-root]

Git 子服务：

![access-subsrv-git][access-subsrv-git]

All Done!

---

[easytier docs]: https://www.easytier.cn/index.html
[gitea docs - deployment]: https://docs.gitea.com/zh-cn/installation/install-from-binary/
[gitea systemd service]: https://github.com/go-gitea/gitea/blob/main/contrib/service/systemd/gitea.service
[caddy docs]: https://caddyserver.com.cn/docs/
[caddy systemd service]: https://caddyserver.com.cn/docs/running
[用户访问网站的完整流程]: https://www.cnblogs.com/yihr/p/9720715.html

[access-root]: /images/gitea-cadddy-的简易配置指南/access-root.png
[access-subsrv-git]: /images/gitea-cadddy-的简易配置指南/access-subsrv-git.png

