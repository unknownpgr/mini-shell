# Mini Shell

유닉스 프로그래밍 수업 과제로 파이프, 리다이렉션 기능을 가진 미니 쉘을 구현하였습니다.

## 설계

기존 방법을 그대로 따르면 파이프 / 리다이렉션이 두 개 이상 되는 경우를 다루기가 힘듭니다.
그래서 입력된 문자열을 순차적으로 처리하지 않고, 부분적으로 나누어 처리하는 방법을 택했습니다.

### Parser

먼저 그렇게 처리하기 위해, parser라는 함수를 사용합니다.

- parser는 매개변수로 `input(char*)`, `pipeIn(int)`, `pipeOut(int)`을 받습니다.
- `input`은 `ls|grep txt>out.txt`처럼 shell에서 실행할 수 있는 커맨드 문자열입니다.
- 이 함수는 내부적으로 새로운 자식 프로세스를 생성하는데, `pipeIn`, `pipeOut`은 그 자식 프로세스의 `stdin`, `stdout`이 됩니다.
- 반환값은 자식 프로세스의 pid입니다.

이 함수는 다음과 같이 동작합니다.

- 아래와 같은 방법을 통해서 input에서 `|`, `&`, `;`, `>` 등 두 개 이상의 프로그램을 실행시키도록 하는 연산자를 하나 찾습니다.

  - 문자열을 앞에서부터 탐색하며 그런 연산자를 찾는다.

  - 만약 연산자가 여러 개 발견되면 우선순위는 `;`가 제일 높고, 다음이 `|`,`&`이며, `<`,`>`가 제일 낮습니다. 즉 `ls|grep txt>out.txt`이 문자열은 `ls|(grep txt>out.txt)`와 같이 해석됩니다.

  - 만약 같은 우선순위의 연산자가 여러 개 있으면 가장 나중에 나오는 연산자를 선택합니다.

- 이렇게 선택한 연산자를 기준으로 문자열을 앞뒤 두 부분으로 나눈 후 적절히 처리합니다.

  - `|`, `&`, `;`의 경우, 나눈 두 부분을 각각 parser로 재귀적으로 호출합니다. 예를 들어 `parse("ls|grep txt>out.txt",~~,~~)`는 내부적으로 `parse("ls",~~,~~)`, `parse("grep txt>out.txt",~~,~~)`가 호출됩니다.

  - `>`,`<`의 경우 파일을 열어 입출력을 수행하는 자식 프로세스를 생성한 후 호출됩니다.

  - `|`, `>`처럼 프로세스간 연결이 필요한 경우 parser를 호출하기 전 pipe를 먼저 생성하여 각 parser를 호출할 때 넘겨줍니다.

  - 필요할 경우 `waitpid`함수를 사용하여 생성한 자식 프로세스가 종료될 때까지 대기해줍니다.

- 이렇게 재귀 호출하다보면 parser는 최종적으로 연산자가 하나도 포함되지 않은 단일한 명령어를 받게 됩니다. 그런 경우 `execvp`함수를 사용하여 명령어를 실행합니다.

### main

`main`함수에서는 무한 루프를 돌면서 다음 과정을 반복합니다.

1. `msh # `과 같은 쉘 기호를 표시해줍니다.
1. `fgets`함수를 호출하여 사용자 입력을 읽습니다.
1. `parser`함수를 호출한 후 `waitpid`함수를 사용하여 대기합니다.

## 검증

완성한 미니쉘이 올바르게 동작하는지 확인하고자 아래 예제를 실행하여 확인해보았습니다.

- 단순 명령어 실행

```
msh # ls
CMakeCache.txt  CMakeFiles  CMakeLists.txt  Makefile  README.md  cmake_install.cmake  log.log  log2.log  mini_sh.c  mini_sh.o  output.log
msh #
```

- 파이프

```
msh # ls|grep txt
CMakeCache.txt
CMakeLists.txt
msh #
```

- 리다이렉션

```
msh # echo hello world! > log.log
msh # cat < log.log
hello world!
msh #
```

- 백그라운드 실행

```
msh # ping 8.8.8.8 & ping 1.1.1.1
PING 8.8.8.8 (8.8.8.8) 56(84) bytes of data.
PING 1.1.1.1 (1.1.1.1) 56(84) bytes of data.
64 bytes from 1.1.1.1: icmp_seq=1 ttl=53 time=7.87 ms
64 bytes from 8.8.8.8: icmp_seq=1 ttl=113 time=44.5 ms
64 bytes from 1.1.1.1: icmp_seq=2 ttl=53 time=9.99 ms
64 bytes from 8.8.8.8: icmp_seq=2 ttl=113 time=45.1 ms
```

- 백그라운드 실행 후 쉘 복귀 (`ping 8.8.8.8&`을 입력한 후 쉘로 복귀하여 잠깐 기다린 후에 `ping 1.1.1.1`을 입력하였습니다.)

```
msh # ping 8.8.8.8&
msh # PING 8.8.8.8 (8.8.8.8) 56(84) bytes of data.
64 bytes from 8.8.8.8: icmp_seq=1 ttl=113 time=43.5 ms
pi64 bytes from 8.8.8.8: icmp_seq=2 ttl=113 time=46.4 ms
ng 64 bytes from 8.8.8.8: icmp_seq=3 ttl=113 time=44.1 ms
1.1.1.164 bytes from 8.8.8.8: icmp_seq=4 ttl=113 time=41.9 ms
64 bytes from 8.8.8.8: icmp_seq=5 ttl=113 time=45.1 ms

PING 1.1.1.1 (1.1.1.1) 56(84) bytes of data.
64 bytes from 1.1.1.1: icmp_seq=1 ttl=53 time=7.07 ms
64 bytes from 8.8.8.8: icmp_seq=6 ttl=113 time=45.6 ms
```

- 복합 명령어 실행

```
msh # ls
CMakeCache.txt  CMakeFiles  CMakeLists.txt  Makefile  README.md  cmake_install.cmake  log1.log  log2.log  mini_sh.c  mini_sh.o
msh # ls>ls.log;grep log>lslog.log<ls.log
msh # cat lslog.log
log1.log
log2.log
ls.log
msh #
```

- type

```
msh # type lslog.log
log1.log
log2.log
ls.log
msh #
```

- cd

```
msh # cd /
msh # ls
bin  boot  dev  etc  home  init  lib  lib32  lib64  lost+found  media  mnt  opt  proc  root  run  sbin  snap  srv  sys  tmp  usr  var
```
