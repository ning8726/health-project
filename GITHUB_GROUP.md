# 把项目作为小组项目上传到 GitHub

这个文件夹已经是一个本地 Git 仓库，当前 `main` 分支包含完整项目。下面只需
在 GitHub 新建一个空仓库，然后把本地历史推送上去。

## 1. 项目负责人创建仓库

1. 登录 GitHub，点击右上角 `+`，选择 **New repository**。
2. 名称填写例如 `health-project`。
3. 选择 Private 或 Public。
4. 不要勾选自动生成 README、`.gitignore` 或 License。
5. 点击 **Create repository**。

创建后复制仓库地址，例如：

```text
https://github.com/你的用户名/health-project.git
```

## 2. 首次上传完整项目

在 PowerShell 中运行：

```powershell
cd "$env:USERPROFILE\Desktop\Health_Project"
git remote add origin https://github.com/你的用户名/health-project.git
git push -u origin main
git push --tags
```

如果提示 `origin already exists`：

```powershell
git remote set-url origin https://github.com/你的用户名/health-project.git
git push -u origin main
git push --tags
```

第一次推送会打开浏览器，要求登录 GitHub。不要把 GitHub 密码或 Token
写进项目文件。

## 3. 邀请小组成员

进入 GitHub 仓库：

1. 打开 **Settings**。
2. 左侧选择 **Collaborators and teams**。
3. 点击 **Add people**。
4. 输入每位成员的 GitHub 用户名。
5. 普通组员选择 **Write** 权限。

成员会收到邀请邮件，接受后才能推送。不要多人共用一个 GitHub 账号。

如果学校或课程需要统一管理很多小组，可以创建 GitHub Organization，
再创建 Team 并把仓库授予 Team。普通课程项目直接邀请 Collaborator
通常更简单。

## 4. 每位成员第一次下载项目

每位成员在自己的电脑运行：

```powershell
cd "$env:USERPROFILE\Documents"
git clone https://github.com/你的用户名/health-project.git
cd health-project
```

然后按 `WINDOWS_AND_GIT.md` 安装 Arduino 库并运行项目。

## 5. 组员修改代码的正确流程

每次开始工作前先更新：

```powershell
git switch main
git pull
git switch -c feature/你的名字-修改内容
```

修改并测试后提交：

```powershell
git status
git diff
git add .
git commit -m "说明这次完成了什么"
git push -u origin feature/你的名字-修改内容
```

推送后 GitHub 会显示 **Compare & pull request**。点击它，填写修改说明，
让项目负责人检查后合并到 `main`。

## 6. 避免小组冲突

- 每个人使用自己的分支，不要所有人直接修改 `main`。
- 修改前先执行 `git pull`。
- 不要使用 `git push --force`。
- 一次提交只处理一个明确功能。
- 上传前检查 `git status`，不要把密码、Wi-Fi 密码或个人 Token 上传。
- 建议负责人在仓库 **Settings > Branches** 中保护 `main`，要求通过
  Pull Request 并检查后再合并。
- 可以用 GitHub Issues 给每个人分配任务，用 Projects 查看完成进度。

## 7. 最常用的五条命令

```powershell
git status
git pull
git add .
git commit -m "修改说明"
git push
```
