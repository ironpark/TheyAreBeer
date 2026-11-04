TAB 최적화 패치 — 소스 (source)
==================================
제작: ddolddolddol-e (MIT 라이선스 — LICENSE.txt)

이 폴더는 패치를 직접 고쳐 쓰고 싶은 분을 위한 소스입니다. 이 패치에 들어간 코드만 있습니다.
(소스의 주석은 지웠습니다. 각 파일이 하는 일은 아래에 정리했습니다.)

■ 빌드
  - 물리 DLL: Visual Studio 2019/2022(또는 Build Tools)의 「C++ 데스크톱 개발」 → powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Target chipmunk
      결과 native\out\DXPhysicsNative.dll (+ 비교 도구 compare.exe)
  - 엔진 모드: 위 도구의 MSBuild(Roslyn C# 컴파일러) + 내 TABModLoader 설치에서 가져온 참조 파일 3개
      (TABModLoader 가 고친 TheyAreBillions.exe, 로더 Lib 폴더의 DXVision.dll · 0Harmony.dll)를 한 폴더에 모은 뒤
      powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Target mod -RefDir <그 폴더>   → mod\out\TABPerf.dll

■ 물리 DLL (native)
  - chipmunk\        Chipmunk2D 7.0.0(MIT, chipmunk\LICENSE.txt). 원작 게임 DLL 과 같은 엔진이며 바꾼 곳:
                     · 충돌 거르기(cpShapeFilterReject): 두 분류/마스크 조합 중 하나라도 맞으면 충돌 — 원작 게임 DLL 과 같은 규칙
                     · cpSpaceStep.c: 위치·속도 적분, 모양 갱신, 접촉 준비를 여러 코어로 · 격자로 충돌 쌍 찾기 + 좁은 판정 병렬 ·
                       접촉 기록 병렬 · 오래된 접촉 정리 병렬 · 충격 풀이를 블록 단계(바둑판 4단계)로 병렬 · 몸체 지우기를 다음 단계 처음에 한꺼번에
                     · cpBBTree.c: 병렬 단계에서 질의용 나무의 잎만 갱신(잎 상자 여유를 넉넉히 — 다시 넣기 횟수 감소)
                     · cpHashSet.c: 접촉 목록 해시 집합의 미리 읽기·병렬 거르기 도우미
  - dxphysics\DXPhysicsNative.c  게임이 부르는 함수 44개(원작 래퍼와 같은 동작) + DX_SetOption(번호, 값) 스위치
                     0 일꾼 수 · 1 독립 반복 병렬 · 2 접촉 목록 잇기 생략 · 3 병렬 충격 풀이 · 4 지연 몸체 지우기 · 5 병렬 충돌 찾기 ·
                     6 병렬 접촉 기록 · 7 블록 단계 풀이 · 8 병렬 오래된 접촉 정리 · 9 블록 순서 번갈아 · 10 넉넉한 잎 상자(11 속도 계수, 12 크기 여유)
                     스위치 3·5·6·8 을 끄면 원작 DLL 과 위치·속도가 비트 단위로 같습니다(harness 로 확인).
  - dxphysics\dxpool.c  일꾼 풀. 일꾼 수 자동 = 물리 코어 − 1, 프로세스 선호도 − 1, 16 중 작은 것(환경 변수 DX_PHYS_THREADS 로 바꿈, 0 = 끔).
                     기다림은 시간 기준(60 µs 돌다가 잠), Windows 8 미만이면 Sleep 으로 대신.
  - dxphysics\dxdiag.c  이 DLL 안에서 오류가 나면 %APPDATA%\TABPerf\dxphysics-crash.txt 에 스택을 남김(폴더가 이미 있을 때만 — 만들지 않음).
                     중요: 코드 영역 맨 앞 8 KB 를 비워 두는 함수(dx_text_pad)가 있습니다 — TABModLoader 가 이 DLL 의 시작 + 0x13B3 에 5바이트를 덮어쓰기 때문에
                     그 자리에 실제 코드가 있으면 게임이 꺼집니다. 지우지 마세요.
  - harness\compare.c   원작 DLL 과 이 DLL 을 한 프로세스에 올려 같은 장면을 돌리고 모든 몸체·질의·충돌 콜백을 비교합니다.
                     compare.exe <원작 DLL> <새 DLL> [장면|all] [몸체 수] [단계 수] · compare.exe prof <계측 판 DLL> ... · compare.exe ray <DLL>

■ 엔진 모드 (mod) — 원작 게임 함수를 Harmony 로 감쌉니다
  - PerfMod.cs       진입점(IModEntry). 물리 DLL 연결(PhysicsRedirect)을 먼저 하고, 아래 손질을 하나씩 설치(실패한 것만 원작 그대로).
  - PhysicsRedirect.cs 게임의 물리 P/Invoke 호출을 모드 폴더의 엔진(chipmunk\ · rapier\)으로 돌림. 같은 선언을 전체 경로로 다시 만들고,
                     부르는 게임 메서드의 호출 대상만 Harmony 트랜스파일러로 바꿈(게임 파일은 그대로).
  - SmoothTime.cs    게임 옵션 「빠른 타이머」가 켜져 있을 때 게임 시간을 실제 경과 시간으로(4 ms 계단 → 매끈하게, 원작 감속 규칙 그대로).
  - ParallelFix.cs   원작 병렬 반복(논리·물리)을 작은 조각으로 나눠 일꾼이 하나씩 가져가게(성능/효율 코어가 섞인 CPU). 그리기용 병렬은 원작 그대로.
  - PhysicsTime.cs   물리 보간 기준을 그 단계가 계산한 시각으로. %APPDATA%\TABPerf\engine.json {"physicsIterations": N} 로 물리 반복 수(없으면 원작 10).
  - ZSort.cs, ZOrder.cs  그리기 순서: Z 변경이 많을 때만 몰아서 1~4 프레임에 한 번 안정 계수 정렬(같은 Z 는 순서 유지), 적을 때는 원작 방식.
  - SoundIdle.cs     프레임마다 소리 장치를 다시 켜는 원작 호출을 같은 값이면 0.5초에 한 번만.
  - FarField.cs      좀비 적 찾기 건너뛰기: 1초마다 사람·방벽에서의 거리 지도를 만들어 닿을 수 없는 좀비는 원작 찾기를 건너뜀(결과 같음).
                     (먼 좀비 몸체 떼기 기능도 들어 있으나 기본 꺼짐.)
  - EnergyFix.cs, EnergyLinks.cs  전력망 연결 질문을 번호표(연결 요소)로 답함 — 원작 길 찾기와 같은 답, 섬이 생길 때의 끊김 제거.
  - LifeMarkers.cs, GameOptions.cs  게임 옵션 그래픽 탭의 「좀비 체력바 가볍게」(기본 꺼짐) — 켜면 좀비 체력바를 얇은 막대 하나로.
  - Bench.cs         성능 시험(기본 꺼짐): 마지막 웨이브 바로 발생, 초당 기록(bench.csv), 자동 시험(고정 생존 게임, 최대 줌, 시간 되면 종료).
                     scripts\bench.ps1 이 엔진별로 돌림.
  - Settings.cs      설정 읽기: 환경 변수(TABPERF_ENGINE, TABPERF_BENCH_...)가 있으면 그 값, 없으면 engine.json.
  - Watchdog.cs      멈춤 진단(기본으로 켜지 않음). Clock.cs · Log.cs(%APPDATA%\TABPerf\billions.log) · MiniJson.cs 는 도우미.
  - 컴파일 기호 PERFMOD 로 빌드합니다(로그·설정 폴더 TABPerf).

■ 라이선스
  - 이 패치의 코드: LICENSE.txt(MIT). 고쳐 쓰고 다시 배포해도 됩니다.
  - Chipmunk2D: native\chipmunk\LICENSE.txt(MIT).
  - They Are Billions 는 Numantian Games 의 게임입니다. 이 패치는 비공식이며 게임 파일을 포함하지 않습니다.


English: this folder contains the patch's own source code only (comments removed; see the file roles above).
Build the physics DLL with scripts\build.ps1 -Target chipmunk (MSVC x64) and the engine mod with scripts\build.ps1 -Target mod -RefDir <folder with your TABModLoader-patched
TheyAreBillions.exe, DXVision.dll and 0Harmony.dll>. Keep dx_text_pad in dxdiag.c: TABModLoader overwrites 5 bytes at DLL base + 0x13B3.
Patch code: MIT (LICENSE.txt). Chipmunk2D: MIT. Unofficial; not affiliated with Numantian Games.
