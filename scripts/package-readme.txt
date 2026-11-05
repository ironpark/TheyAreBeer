TAB 최적화 패치 (TABPerf) v{version}
https://github.com/ironpark/TheyAreBeer

설치
1. They Are Billions에 TABModLoader(1.1.x)를 설치합니다. https://codeberg.org/JKstring/TABModLoader
2. 이 압축 파일의 TABPerf 폴더를 <게임 설치 폴더>\Mods\ 안에 그대로 넣습니다.
   (<게임 설치 폴더>\Mods\TABPerf\TABPerf.dll 이 되어야 합니다.)
3. 게임을 실행합니다. 적용 기록은 %APPDATA%\TABPerf\billions.log 에 남습니다.

설정 (%APPDATA%\TABPerf\engine.json, 없으면 만듭니다. 게임을 다시 시작하면 적용)
  "physicsEngine": "chipmunk"(기본) | "rapier" | "original"
  "renderer": "dxvk"(기본, Direct3D 9 on Vulkan) | "d3d9"(시스템 Direct3D 9)
예: { "physicsEngine": "rapier" }

지우기: <게임 설치 폴더>\Mods\TABPerf 폴더를 지웁니다.

라이선스: licenses 폴더 (이 패치 MIT, Chipmunk2D MIT, Rapier Apache-2.0, DXVK zlib).
They Are Billions는 Numantian Games의 게임이며, 이 패치는 비공식입니다.
