---
title: "Hexo 使用 iframe 添加音视频"
date: 2026-11-11 11:00:00
categories:
- 技术
tags:
- hexo
- html
- iframe
---

单纯记录一下，不涉及任何讲解，只需要把下面这段代码插入 Markdown 文档即可：

```html
{% raw %}
<div id="demo" style="position: relative; width: 100%; height: 0; padding-bottom: 75%;">
    <iframe src="换成本地/第三方视频链接"
        scrolling="no" border="0" frameborder="no" framespacing="0" allowfullscreen="true" 
        style="position: absolute; width: 100%; height: 100%; Left: 0; top: 0;" >
    </iframe>
</div>
{% endraw %}
```

比如下面这首 "Гипербореи" 的插入方式:

<!-- more -->

{% raw %}
<div id="demo" style="position: relative; width: 100%; height: 0; padding-bottom: 75%;">
    <iframe src="https://www.bilibili.com/video/BV1ufxpzaEwf"
        scrolling="no" border="0" frameborder="no" framespacing="0" allowfullscreen="true" 
        style="position: absolute; width: 100%; height: 100%; Left: 0; top: 0;" >
    </iframe>
</div>
{% endraw %}

