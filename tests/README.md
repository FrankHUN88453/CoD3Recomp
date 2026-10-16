# Bot helper regression checks

From a Visual Studio developer command prompt with LLVM on PATH:

```bat
clang-cl /nologo /EHsc tests/bot_weapon_selection.cpp /Fe:%TEMP%/cod3-weapon-test.exe /Fo:%TEMP%/cod3-weapon-test.obj
%TEMP%/cod3-weapon-test.exe
clang-cl /nologo /EHsc tests/bot_team_assignment.cpp /Fe:%TEMP%/cod3-team-test.exe /Fo:%TEMP%/cod3-team-test.obj
%TEMP%/cod3-team-test.exe
```

Run without NDEBUG so the team assertions are active. These test the actual
selection helpers; they do not establish in-game rendering or network behavior.
