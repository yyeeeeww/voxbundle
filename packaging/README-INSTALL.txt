VOX BUNDLE - 10 vocal Audio Units for macOS (Apple Silicon + Intel)

Установка (Terminal, из папки с распакованным архивом):

    cd ~/Downloads/VoxBundle
    xattr -dr com.apple.quarantine .
    ./install.command

Затем в FL Studio: Options > Manage plugins > Find plugins (AU).
Плагины появятся у производителя "Vox Bundle".

VoxComp   компрессор / де-эссер / лимитер
VoxEQ     эквалайзер
VoxHeat   сатурация
VoxSpace  ревёрб / дилей
VoxSynth  вокодер
VoxDouble даблер
VoxTune   автотюн
VoxPitch  питч-шифтер
VoxDevil  девил-компрессор
VoxAdlib  обработка эдлибов

DEVIL в каждом плагине включает перегруз.

Удаление: удалите Vox*.component из ~/Library/Audio/Plug-Ins/Components.
