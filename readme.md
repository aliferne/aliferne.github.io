# 简介

目前使用 Hexo & NexT 建站， GitHub 和自托管服务器都有源码，但是推送脚本只推送到 GitHub

- `_config.yml`: hexo 配置
- `deploy.sh`: 部署到网站的脚本
- `public`: 公开内容（即静态网页）
- `push-to-git.sh`: 推送到 github
- `scaffolds`: 似乎是模板文件
- `source`: 我的博客源文件
- `themes`: 主题相关
- `update-locally.sh`: 本地预览


特别的，对于 `source` 文件夹，有如下说明：

- source/
-    images:        博客文章图片
-    `_posts`:      发表的博客
-    categories:    分类
-    aboutme:       个人介绍
-    tags:          标签
-    `_drafts`:     草稿
-    music:         音乐(目前还没开工)
-    links:         个人相关链接
-    code:          博客引用到的代码源文件

图片、代码的子目录应当为 `_posts` 中文章名称(的子集)，这样子方便管理一些

