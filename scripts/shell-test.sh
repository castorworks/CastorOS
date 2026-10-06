#!/bin/bash
# 启动 QEMU，等命令行就绪后向串口输入一串命令，检查命令行的行为（后台任务、Ctrl-C、kill、程序读键盘输入、重定向和管道、引号、标准错误、脚本）。
# 由 make test 调用：
#
#   scripts/shell-test.sh <log> <results> <boot-timeout-seconds> <qemu command...>
#
# QEMU 的全部输出写入 <log>；每项检查在 <results> 里留一行 "shelltest: <名字>: ok|FAILED"，
# 全部通过时最后一行是 "shelltest: all passed"。命令行没有起来时 <results> 为空。
# 不按固定时间等待：每一步都等日志里出现预期的那一行（最多 STEP_TIMEOUT 秒）。

LOG=$1; RESULTS=$2; BOOT_TIMEOUT=$3; shift 3
STEP_TIMEOUT=${STEP_TIMEOUT:-30}
FIFO=$LOG.stdin

rm -f "$FIFO"; mkfifo "$FIFO" || exit 1
: > "$LOG"; : > "$RESULTS"
"$@" < "$FIFO" > "$LOG" 2>&1 &
QEMU_PID=$!
exec 3> "$FIFO"
trap 'kill $QEMU_PID 2>/dev/null; wait $QEMU_PID 2>/dev/null; exec 3>&-; rm -f "$FIFO"' EXIT

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
    send 'abc\003'; expect '^write: killed by signal 2$'
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
    send 'ls | grep selftest\n'; expect '^ *[0-9]+  selftest$' || return 1
    send 'hello a b | grep argv\n'; expect 'argv\[2\] = b$'
}
check "pipes" pipes

# ---- 目录：启动映像里带来的，和现建的 ----
directories() {
    send 'ls docs\n'; expect '^ *[0-9]+  paths.txt$' || return 1
    send 'cat docs/paths.txt | grep current\n'; expect '^There is no current directory' || return 1
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
    send 'greet world again\n'; expect '^hello world$' && expect '^from greet, second again$' || return 1
    send 'write outer "greet nested"\n'; expect '> $' || return 1
    send 'outer\n'; expect '^hello nested$' || return 1
    send 'greet later &\n'; expect '(^|> )hello later$' && expect '\] done  greet$' || return 1
    send 'write forever forever\n'; expect '> $' || return 1
    send 'forever\n'; expect '^sh: forever: scripts nested too deeply$' || return 1
    send 'greet | wc\n'; expect 'is a script: it cannot be piped or redirected$'
}
check "scripts" scripts

script_interrupt() {
    send 'write slow "sleep 60"\n'; expect '> $' || return 1
    send 'echo "echo not reached" >> slow\n'; expect '> $' || return 1
    send 'slow\n'; sleep 0.5
    send '\003'; expect '^sleep: killed by signal 2$' || return 1
    send 'echo after the script\n'; expect '^after the script$' || return 1
    ! output | grep -aq '^not reached$'
}
check "ctrl-c stops a script" script_interrupt

[ $failed -eq 0 ] && echo "shelltest: all passed" >> "$RESULTS"
exit 0
