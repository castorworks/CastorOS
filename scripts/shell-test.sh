#!/bin/bash
# 启动 QEMU，等命令行就绪后向串口输入一串命令，检查命令行的行为（后台任务、Ctrl-C、kill、程序读键盘输入、重定向和管道、引号、标准错误、脚本），最后重启一次、关机。
# 由 make test 调用：
#
#   scripts/shell-test.sh <log> <results> <boot-timeout-seconds> <qemu command...>
#
# QEMU 的全部输出写入 <log>；每项检查在 <results> 里留一行 "shelltest: <名字>: ok|FAILED"，
# 全部通过时最后一行是 "shelltest: all passed"。命令行没有起来时 <results> 为空。
# 不按固定时间等待：每一步都等日志里出现预期的那一行（最多 STEP_TIMEOUT 秒）。
#
# 环境变量 MONITOR（可选）：QEMU 监视器的管道，QEMU 命令里要有 -monitor pipe:$MONITOR。
# 有它才检查键盘和电源键：监视器的 sendkey 命令在虚拟机的键盘上敲键，system_powerdown 按电源键。
# 环境变量 KBD_DRIVER（可选）：敲的键到的是哪个驱动，kbd（PS/2，不写就是它）或 usbkbd
# （虚拟机上接了 USB 键盘时，QEMU 把键送给它）；none 表示这台虚拟机没有键盘。
# 环境变量 POWER_OFF（可选）：最后怎么关机，button（按电源键，要有监视器）或 command
# （敲 poweroff，不写就是它）。

LOG=$1; RESULTS=$2; BOOT_TIMEOUT=$3; shift 3
STEP_TIMEOUT=${STEP_TIMEOUT:-30}
FIFO=$LOG.stdin
KBD_DRIVER=${KBD_DRIVER:-kbd}
# 这个驱动可以收键了的那一行。USB 键盘要等驱动在端口上认出它
KBD_READY='kbd: driver ready'
[ "$KBD_DRIVER" = usbkbd ] && KBD_READY='usbkbd: keyboard ready'

rm -f "$FIFO"; mkfifo "$FIFO" || exit 1
if [ -n "$MONITOR" ]; then
    # QEMU 从 .in 读命令，把应答写到 .out；应答没人读的话管道写满了它会卡住
    rm -f "$MONITOR.in" "$MONITOR.out"; mkfifo "$MONITOR.in" "$MONITOR.out" || exit 1
    exec 4<> "$MONITOR.in"
    cat "$MONITOR.out" > /dev/null &
    DRAIN_PID=$!
fi
: > "$LOG"; : > "$RESULTS"
"$@" < "$FIFO" > "$LOG" 2>&1 &
QEMU_PID=$!
exec 3> "$FIFO"
trap 'kill $QEMU_PID $DRAIN_PID 2>/dev/null; wait $QEMU_PID 2>/dev/null; exec 3>&- 4>&-; rm -f "$FIFO" "$MONITOR.in" "$MONITOR.out"' EXIT

MARK=0          # 日志里的字节位置：expect 只看这之后的输出
MATCH=          # 最近一次 expect 匹配到的那一行
failed=0

# 位置 MARK 之后的输出，去掉颜色转义和回车
output() { tail -c +$((MARK + 1)) "$LOG" | sed $'s/\x1b\\[[0-9;]*m//g' | tr -d '\r'; }

# expect <扩展正则> [秒]：等到 MARK 之后有一行匹配
expect() {
    local deadline=$((SECONDS + ${2:-$STEP_TIMEOUT}))
    while :; do
        MATCH=$(output | grep -aE -m1 -- "$1") && return 0
        [ $SECONDS -ge $deadline ] && return 1
        kill -0 $QEMU_PID 2>/dev/null || return 1
        sleep 0.2
    done
}

# send <printf 格式串>：记下当前位置，然后把输入写给串口
send() { MARK=$(wc -c < "$LOG"); printf "$1" >&3; }

# keys <键名...>：记下当前位置，然后在虚拟机的键盘上敲这些键（键名是 QEMU sendkey 的）
keys() {
    MARK=$(wc -c < "$LOG")
    local key
    for key in "$@"; do echo "sendkey $key" >&4; sleep 0.1; done
}

# check <名字> <命令...>：记录一项检查的结果
check() {
    local name=$1; shift
    if "$@"; then echo "shelltest: $name: ok" >> "$RESULTS"
    else echo "shelltest: $name: FAILED" >> "$RESULTS"; failed=1; fi
}

# "[12] sleep" 这样的一行里的进程号
job_pid() { echo "$MATCH" | sed -E 's/.*\[([0-9]+)\].*/\1/'; }

expect "sh: ready" "$BOOT_TIMEOUT" || exit 0

# ---- 前台运行 ----
run_program() { send 'echo one two\n'; expect '^one two$'; }
check "run a program" run_program

unknown_command() { send 'nosuchprogram\n'; expect '^nosuchprogram: unknown command'; }
check "unknown command" unknown_command

# ---- clear 输出的是清屏的转义序列（output 只去掉颜色的，这两个留着）----
clear_screen() { send 'clear\n'; expect $'\x1b\\[H\x1b\\[2J'; }
check "clear" clear_screen

# ---- 后台任务：启动后提示符马上可用，jobs 能看到它，结束时有报告 ----
background_job() {
    send 'sleep 2 &\n'; expect '\[[0-9]+\] sleep$' || return 1
    local pid start=$MARK; pid=$(job_pid)
    send 'jobs\n'; expect "^\[$pid\] running  sleep$" || return 1
    send 'echo still here\n'; expect '^still here$' || return 1
    MARK=$start; expect "\[$pid\] done  sleep$" || return 1
    send 'jobs\n'; expect '^\(no background jobs\)$'
}
check "background job" background_job

# ---- Ctrl-C 终止前台程序，之后命令行照常工作 ----
ctrl_c_foreground() {
    send 'sleep 60\n'; expect 'sleep 60$' || return 1      # 命令行回显了这一行：已经读到
    sleep 0.5
    send '\003'; expect '^sleep: killed by signal 2$' || return 1
    send 'echo after\n'; expect '^after$'
}
check "ctrl-c stops the foreground program" ctrl_c_foreground

# ---- Ctrl-C 在提示符下放弃已经输入的半行 ----
ctrl_c_prompt() {
    send 'nosuch'; expect 'nosuch$' || return 1
    send '\003'
    send 'echo fresh\n'; expect '^fresh$'       # 没有放弃的话这一行会是 "nosuchecho fresh"
}
check "ctrl-c discards the line" ctrl_c_prompt

# ---- kill 终止后台任务；不能终止 init ----
kill_job() {
    send 'sleep 60 &\n'; expect '\[[0-9]+\] sleep$' || return 1
    local pid; pid=$(job_pid)
    send "kill $pid\\n"; expect '^sleep: killed by signal 9$' || return 1
    send 'jobs\n'; expect '^\(no background jobs\)$'
}
check "kill a background job" kill_job

kill_init() { send 'kill 1\n'; expect '^kill: cannot kill 1$'; }
check "kill init is refused" kill_init

# ---- 被 Ctrl-C 终止的服务留下的监听端口可以马上重新使用 ----
listener_reclaimed() {
    send 'echod 7 1\n'; expect '^echod: listening on port 7$' || return 1
    send '\003'; expect '^echod: killed by signal 2$' || return 1
    send 'echod 7 1 &\n'; expect '\[[0-9]+\] echod$' || return 1
    local pid; pid=$(job_pid)
    expect 'echod: listening on port 7$' || return 1     # 前面可能带着提示符
    send "kill $pid\\n"; expect '^echod: killed by signal 9$'
}
check "killed server's port is reusable" listener_reclaimed

# ---- 前台程序读键盘输入：按行读、退格、行首 Ctrl-D 结束 ----
program_input() {
    send 'write notes\n'; expect '^\(type lines' || return 1
    send 'first line\n'; expect '^first line$' || return 1
    send 'secx\177ond\n\004'; expect '> $' || return 1
    send 'cat notes\n'; expect '^first line$' && expect '^second$'
}
check "program reads keyboard input" program_input

# ---- 程序启动前就敲进来的输入归它，它没读的部分回到命令行 ----
typed_ahead() {
    send 'write burst\nline one\nline two\n\004echo back at the prompt\n'
    expect '^back at the prompt$' || return 1
    send 'cat burst\n'; expect '^line one$' && expect '^line two$' || return 1
    send 'sleep 1\necho after sleep\n'; expect '^after sleep$'
}
check "typed-ahead input" typed_ahead

# ---- 后台任务读不到输入；读输入的前台程序可以用 Ctrl-C 终止 ----
background_input() {
    send 'write nothing &\n'; expect '\[[0-9]+\] done  write$' || return 1
    send 'write nothing\n'; expect '^\(type lines' || return 1
    # 不要求这一行从行首开始：程序可能已经读走并回显了 Ctrl-C 之前敲的字符
    # （几个 CPU 的时候它和串口驱动同时在跑，赶得上），它们就在这一行的前面
    send 'abc\003'; expect 'write: killed by signal 2$'
}
check "input goes to the foreground only" background_input

# ---- 重定向：输出到文件、追加、从文件输入 ----
redirection() {
    send 'echo hello there > out1\n'; expect '> $' || return 1
    send 'echo more >>out1\n'; expect '> $' || return 1
    send 'cat out1\n'; expect '^hello there$' && expect '^more$' || return 1
    send 'wc < out1\n'; expect '^2 3 17$' || return 1
    send 'cat < nosuchfile\n'; expect '^cannot read nosuchfile$'
}
check "redirection" redirection

# ---- 管道：两段、三段、和重定向一起用 ----
pipes() {
    send 'cat out1 | wc\n'; expect '^2 3 17$' || return 1
    send 'cat < out1 | grep more | wc > count\n'; expect '> $' || return 1
    send 'cat count\n'; expect '^1 1 5$' || return 1
    send 'ls /bin | grep selftest\n'; expect '^ *[0-9]+  selftest$' || return 1
    send 'hello a b | grep argv\n'; expect 'argv\[2\] = b$'
}
check "pipes" pipes

# ---- 目录：系统自带的，和现建的 ----
directories() {
    send 'ls /usr/share/doc\n'; expect '^ *[0-9]+  paths.txt$' || return 1
    send 'cat /usr/share/doc/paths.txt | grep current\n'; expect 'from the current directory$' || return 1
    send 'mkdir box\n'; expect '> $' || return 1
    send 'write box/note hello\n'; expect '> $' || return 1
    send 'ls | grep box\n'; expect '^ +box/$' || return 1
    send 'ls box\n'; expect '^ *6  note$' || return 1
    send 'rm box\n'; expect 'rm: ' || return 1                # 目录里还有东西：删不掉
    send 'rm box/note\n'; expect '> $' || return 1
    send 'rm box\n'; expect '> $' || return 1
    send 'ls box\n'; expect '^\(nothing in box\)$' || return 1
    send 'mkdir nowhere/inside\n'; expect '^mkdir: cannot create nowhere/inside$'
}
check "directories" directories

# ---- 目录树：根下面是 Linux 那样的几个目录；/tmp 和别处不在同一个文件服务上 ----
tree() {
    send 'ls /\n'; expect '^ +bin/$' && expect '^ +etc/$' && expect '^ +home/$' && expect '^ +tmp/$' &&
        expect '^ +usr/$' || return 1
    send 'cat /etc/rc\n'; expect '^selftest$' || return 1
    send 'write /home/kept.txt on the disk\n'; expect '> $' || return 1
    send 'write /tmp/scratch.txt in memory\n'; expect '> $' || return 1
    send 'cat /home/kept.txt /tmp/scratch.txt\n'; expect '^on the disk$' && expect '^in memory$' || return 1
    send 'cd /tmp\n'; expect '> $' || return 1
    send 'ls\n'; expect '^ *[0-9]+  scratch.txt$' || return 1
    send 'cp scratch.txt /home/copy.txt\n'; expect '> $' || return 1
    send 'cd /home\n'; expect '> $' || return 1
    send 'cat copy.txt\n'; expect '^in memory$' || return 1
    send 'rm copy.txt kept.txt /tmp/scratch.txt\n'; expect '> $' || return 1
    send 'cd /\n'; expect '> $'
}
check "directory tree" tree

# ---- 当前目录：cd、pwd；程序从命令行所在的目录开始，换了目录命令照样找得到 ----
current_directory() {
    send 'pwd\n'; expect '^/$' || return 1
    send 'cd usr/share/doc\n'; expect '> $' || return 1
    send 'pwd\n'; expect '^/usr/share/doc$' || return 1
    send 'ls\n'; expect '^ *[0-9]+  paths.txt$' || return 1
    send 'cat paths.txt | grep current\n'; expect 'from the current directory$' || return 1
    send 'write here.txt inside doc\n'; expect '> $' || return 1
    send 'cat /usr/share/doc/here.txt\n'; expect '^inside doc$' || return 1
    send 'cat ../doc/readme.txt | wc > count\n'; expect '> $' || return 1  # 重定向的文件名也是相对的
    send 'ls | grep count\n'; expect '^ *[0-9]+  count$' || return 1
    send 'cd nosuch\n'; expect '^cd: nosuch: not a directory$' || return 1
    send 'cd ..\n'; expect '> $' || return 1
    send 'pwd\n'; expect '^/usr/share$' || return 1
    send 'cd\n'; expect '> $' || return 1
    send 'pwd\n'; expect '^/$' || return 1
    send 'rm usr/share/doc/here.txt usr/share/doc/count\n'; expect '> $'
}
check "current directory" current_directory

# ---- 改名和移动 ----
rename_files() {
    send 'write old.txt moved text\n'; expect '> $' || return 1
    send 'mv old.txt new.txt\n'; expect '> $' || return 1
    send 'cat new.txt\n'; expect '^moved text$' || return 1
    send 'cat old.txt\n'; expect '^cat: old.txt: no such file$' || return 1
    send 'mkdir crate\n'; expect '> $' || return 1
    send 'mv new.txt crate\n'; expect '> $' || return 1                # 目标是目录：移进去
    send 'mv crate box2\n'; expect '> $' || return 1                   # 目录连同里面的东西
    send 'cat box2/new.txt\n'; expect '^moved text$' || return 1
    send 'mv box2/new.txt /usr/share/doc/paths.txt\n'; expect '^mv: cannot move' || return 1    # 不盖掉已有的
    send 'rm box2/new.txt box2\n'; expect '> $'
}
check "rename and move" rename_files

# ---- 键盘输入进管道的第一段；Ctrl-C 终止管道里的每一段 ----
pipe_keyboard() {
    send 'cat | grep keep > kept\n'; sleep 0.5
    send 'keep this\ndrop this\nkeep that\n\004'; expect '> $' || return 1
    send 'wc < kept\n'; expect '^2 4 20$' || return 1
    send 'sleep 60 | cat\n'; sleep 0.5
    send '\003'; expect '^sleep: killed by signal 2$' && expect '^cat: killed by signal 2$'
}
check "pipes with keyboard input and ctrl-c" pipe_keyboard

# ---- 写错的命令行、后台运行的管道 ----
pipe_errors() {
    send 'ls |\n'; expect '^sh: syntax error$' || return 1
    send 'ls >\n'; expect '^sh: syntax error$' || return 1
    send 'ls | nosuchprogram\n'; expect '^nosuchprogram: unknown command' || return 1
    send 'echo one two | wc &\n'; expect '(^|> )1 2 8$' || return 1
    expect '\] done  wc$' || return 1
    send 'jobs\n'; expect '^\(no background jobs\)$'
}
check "pipe errors and background pipes" pipe_errors

# ---- 引号：空格和运算符原样进参数，可以出现在词的中间，没配对是语法错误 ----
quoting() {
    send 'echo "a  b" c\n'; expect '^a  b c$' || return 1
    send "echo 'x | y > z' end\n"; expect '^x \| y > z end$' || return 1
    send 'echo a"b c"d | wc\n'; expect '^1 2 6$' || return 1
    send 'write quoted "hello   world"\n'; expect '> $' || return 1
    send 'cat quoted\n'; expect '^hello   world$' || return 1
    send 'echo "unterminated\n'; expect '^sh: syntax error$'
}
check "quoting" quoting

# ---- 标准错误：重定向输出时报错仍然在屏幕上；2> 把它收进文件 ----
standard_error() {
    send 'cat nosuchfile > captured\n'; expect '^cat: nosuchfile: no such file$' || return 1
    send 'wc < captured\n'; expect '^0 0 0$' || return 1
    send 'cat nosuchfile | wc\n'; expect '^cat: nosuchfile: no such file$' && expect '^0 0 0$' || return 1
    send 'cat nosuchfile 2> errors\n'; expect '^cat: exit status 1$' || return 1
    ! output | grep -aq 'no such file' || return 1
    send 'cat errors\n'; expect '^cat: nosuchfile: no such file$' || return 1
    send 'cat nosuchfile quoted 2>> errors > both\n'; expect '^cat: exit status 1$' || return 1
    send 'wc < errors\n'; expect '^2 10 60$' || return 1
    send 'cat both\n'; expect '^hello   world$'
}
check "standard error" standard_error

# ---- 脚本：逐行执行、参数、嵌套、后台、Ctrl-C 停下整个脚本 ----
scripts() {
    send 'write greet "echo hello $1"\n'; expect '> $' || return 1
    send "echo 'echo from \$0, second \$2' >> greet\n"; expect '> $' || return 1
    # 只写名字的命令到 /bin 里找；当前目录里的脚本要写成 ./名字
    send 'greet world\n'; expect '^greet: unknown command' || return 1
    send './greet world again\n'; expect '^hello world$' && expect '^from ./greet, second again$' || return 1
    send 'write outer "./greet nested"\n'; expect '> $' || return 1
    send './outer\n'; expect '^hello nested$' || return 1
    send './greet later &\n'; expect '(^|> )hello later$' && expect '\] done  ./greet$' || return 1
    send 'write forever ./forever\n'; expect '> $' || return 1
    send './forever\n'; expect '^sh: ./forever: scripts nested too deeply$' || return 1
    send './greet | wc\n'; expect 'is a script: it cannot be piped or redirected$'
}
check "scripts" scripts

script_interrupt() {
    send 'write slow "sleep 60"\n'; expect '> $' || return 1
    send 'echo "echo not reached" >> slow\n'; expect '> $' || return 1
    send './slow\n'; sleep 0.5
    send '\003'; expect '^sleep: killed by signal 2$' || return 1
    send 'echo after the script\n'; expect '^after the script$' || return 1
    ! output | grep -aq '^not reached$'
}
check "ctrl-c stops a script" script_interrupt

# ---- 行编辑：方向键移动光标在中间改，Home / End / Delete；上下键翻以前的行 ----
line_editing() {
    send 'echo hllo\033[D\033[D\033[De\n'; expect '^hello$' || return 1
    send 'Xecho home\033[H\033[3~\033[F!\n'; expect '^home!$' || return 1
    send 'echo first\n'; expect '^first$' || return 1
    send 'echo second\n'; expect '^second$' || return 1
    send '\033[A\033[A\n'; expect '^first$' || return 1
    send 'echo typed\033[A\033[B again\n'; expect '^typed again$' || return 1     # 翻回来：敲了一半的还在
    send 'ec hi\033[D\033[D\033[D\t\n'; expect '^hi$' || return 1                 # 补全光标前面的词，后面的留着
    send 'write /tmp/edited\n'; sleep 0.5                                         # 程序读输入时也能改
    send 'lne\033[D\033[Di\033[A\n\004'; expect '> $' || return 1
    send 'cat /tmp/edited\n'; expect '^line$' || return 1
    send 'rm /tmp/edited\n'; expect '> $' || return 1
    send 'echo one two\033b\033[1;5DX\033f\033fY\n'; expect '^Xone twoY$' || return 1  # 按词移动：Alt-B、Ctrl-左、Alt-F
    send 'echo keep cut\033b\013\n'; expect '^keep$' || return 1                  # Ctrl-K 删到行尾
    send 'bad echo word gone\027\001\033f\033f\033b\025\n'; expect '^word$' || return 1   # Ctrl-W 删一个词，Ctrl-U 删到行首
    send 'echo moved here\027\001\033f \031\n'; expect '^here moved$' || return 1   # Ctrl-Y 把删掉的贴到别处
    send '\022fir'; expect "\(search\)'fir': echo first" || return 1              # Ctrl-R 在历史里找
    send '\n'; expect '^first$' || return 1
    send '\022echo \022\022'; expect "\(search\)'echo ': echo word" || return 1   # 再按：更早的
    send '\007echo gave up\n'; expect '^gave up$' || return 1                     # Ctrl-G 放弃
    send 'echo one two\027\001\013echo \031\033y\n'; expect '^two$'                # Alt-Y 把贴的换成更早删的
}
check "line editing and history" line_editing

# ---- Tab 补全：命令名、路径、重定向和管道后面的词；按两次列出候选 ----
completion() {
    send 'cd /\n'; expect '> $' || return 1
    send 'wr\t/tm\tcomp.txt piped\n'; expect 'write /tmp/comp.txt piped$' || return 1    # 唯一的候选：补完，跟一个空格；目录跟 /
    send 'cat /tmp/comp.txt | gr\tpiped > /tm\tcomp2.txt\n'; expect '> $' || return 1
    send 'cat /tmp/comp2.txt\n'; expect '^piped$' || return 1
    send 'ec\t tabbed\n'; expect '^tabbed$' || return 1           # echo 和 echod：补到共同的开头，不跟空格
    send 'ec\t\t\t'; expect '^echo +echod$' && expect '^> echo$' || return 1   # 补不动了再按一次：列出来，这一行还在
    send ' relisted\n'; expect '^relisted$' || return 1
    send 'pw\t\n'; expect '^/$' || return 1                       # 内置命令
    send 'cd /u\ts\td\t\n'; expect 'cd /usr/share/doc/$' || return 1
    send 'cat pa\t| grep current\n'; expect 'from the current directory$' || return 1    # 相对当前目录
    send 'cd pa\t\n'; expect '^cd: pa: not a directory$' || return 1      # cd 后面只补目录，paths.txt 不算
    send 'cd /\n'; expect '> $'
}
check "tab completion" completion

# ---- 补全带空格的名字时加上引号；kill 后面补后台任务的 PID ----
completion_quotes() {
    send 'mkdir "/tmp/two words"\n'; expect '> $' || return 1
    send 'write "/tmp/two words/a file" quoted\n'; expect '> $' || return 1
    send 'cat /tmp/tw\ta\t\n'; expect '^quoted$' || return 1
    send "cat '/tmp/two w\\ta\\t\\n"; expect '^quoted$' || return 1       # 用户自己开的引号照他的来
    send 'rm "/tmp/two words/a file" "/tmp/two words" /tmp/comp.txt /tmp/comp2.txt\n'; expect '> $' || return 1
    send 'sleep 60 &\n'; expect '\[[0-9]+\] sleep$' || return 1
    send 'kill \t\n'; expect '^sleep: killed by signal 9$'
}
check "tab completion: quotes and kill" completion_quotes

# ---- 键盘（PC）：敲的键和串口来的输入走同一条路——Shift、退格、Ctrl-C ----
keyboard_input() {
    MARK=0; expect "$KBD_READY" || return 1
    keys e c h o spc shift-k e y s spc x backspace 1 ret; expect '^Keys 1$' || return 1
    keys h e l l tab ret; expect '^hello from pid' || return 1         # Tab 键：补全
    keys e c h o spc k y left e ret; expect '^key$' || return 1       # 方向键：左移了再插入
    keys x home delete up end 2 ret; expect '^key2$' || return 1      # Home、Delete、上（上一行）、End
    keys e c h o spc a spc b ctrl-left x ret; expect '^a xb$' || return 1     # Ctrl-左：移动一个词
    keys ctrl-r y 2 ret; expect '^key2$' || return 1                  # Ctrl-R：在历史里找
    keys s l e e p spc 6 0 ret; expect 'sleep 60$' || return 1
    sleep 0.5
    keys ctrl-c; expect '^sleep: killed by signal 2$'
}
has_keyboard() { [ -n "$MONITOR" ] && [ "$KBD_DRIVER" != none ]; }
has_keyboard && check "keyboard input" keyboard_input

# ---- 终端输入的模块崩溃了：init 重启它，之后输入照常（selftest restart 让模块退出）----
# 最后做：每个模块最多被重启 5 次
serial_after_restart() {
    send "selftest restart $1\\n"; expect "^selftest: $1 restarted" || return 1
    send 'echo typed after\n'; expect '^typed after$'
}
check "uart driver is restarted" serial_after_restart uart
check "console service is restarted" serial_after_restart console

keyboard_after_restart() {
    send "selftest restart $KBD_DRIVER\\n"; expect "^selftest: $KBD_DRIVER restarted" || return 1
    expect "$KBD_READY" || return 1
    keys e c h o spc k e y s spc a f t e r ret; expect '^keys after$'
}
has_keyboard && check "keyboard driver is restarted" keyboard_after_restart

# ---- 重启：机器复位，重新启动到命令行；根在磁盘上时，重启前写的文件还在 ----
on_disk() { grep -aq 'diskfs: ready' "$LOG"; }

reboot_machine() {
    send 'write /home/kept.txt kept across the reboot\n'; expect '> $' || return 1
    send 'reboot\n'; expect '^init: rebooting$' || return 1
    if on_disk; then expect 'diskfs: stopped$' && expect 'blk: stopped$' || return 1; fi
    expect 'sh: ready' "$BOOT_TIMEOUT" || return 1
    send 'echo up again\n'; expect '^up again$' || return 1
    if on_disk; then send 'cat /home/kept.txt\n'; expect '^kept across the reboot$'; fi
}
check "reboot" reboot_machine

# ---- 关机：敲 poweroff，或者按电源键（电源键驱动请 init 关机）；QEMU 自己退出 ----
qemu_exits() {
    local deadline=$((SECONDS + STEP_TIMEOUT))
    while kill -0 $QEMU_PID 2>/dev/null; do
        [ $SECONDS -ge $deadline ] && return 1
        sleep 0.2
    done
}

power_off() {
    send 'poweroff\n'; expect '^init: powering off$' && qemu_exits
}

power_button() {
    MARK=0; expect "pwrbtn: driver ready" || return 1
    MARK=$(wc -c < "$LOG"); echo system_powerdown >&4
    expect 'pwrbtn: power button pressed$' && expect 'init: powering off$' && qemu_exits
}

if [ "$POWER_OFF" = button ] && [ -n "$MONITOR" ]; then check "power button" power_button
else check "power off" power_off; fi

[ $failed -eq 0 ] && echo "shelltest: all passed" >> "$RESULTS"
exit 0
