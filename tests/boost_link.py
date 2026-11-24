"""Список библиотек Boost для линковки offline-харнесов — с пробой, а не по памяти.

Зачем отдельный модуль. Четыре теста (три p2p и один chain) собирают C++-харнес прямо из Python и
перечисляют библиотеки руками. Список с `boost_system` перестал быть верным: Boost.System с 1.69
header-only, а с 1.90 дистрибутив вообще не поставляет `libboost_system`, и `-lboost_system` даёт
`cannot find -lboost_system` — CI падает на дистрибутиве, где сама нода собирается нормально
(живой случай 24.11.2026, job «consensus_sim + pm unit tests (distro Boost)»).

`CMakeLists.txt` решает это пробой компонента (`find_package(boost_system QUIET CONFIG)`); тест обязан
делать то же самое. Копировать пробу в четыре файла — значит оставить три места, где она состарится
(класс [[fix-list-is-not-a-closed-class]]), поэтому она здесь одна.

`-print-file-name` печатает путь, если библиотека найдена, и ПОВТОРЯЕТ имя, если нет — по этому
различию и решаем. Проверять существование пути обязательно: на некоторых дистрибутивах печатается
путь к отсутствующему файлу.
"""
import os
import subprocess
from pathlib import Path

BASE = (
    'boost_context', 'boost_coroutine', 'boost_thread', 'boost_chrono',
    'boost_filesystem', 'boost_date_time', 'boost_atomic',
    'ssl', 'crypto', 'z', 'pthread', 'dl', 'rt',
)

# Позиция boost_system в историческом списке — сразу после boost_chrono. Порядок для статических
# архивов важен, поэтому вставляем туда, а не в конец.
_INSERT_AFTER = 'boost_chrono'


def have_lib(name, cxx=None):
    """Есть ли у компилятора библиотека `name` (проверка через -print-file-name)."""
    cxx = cxx or os.environ.get('CXX', 'g++')
    try:
        out = subprocess.run([cxx, '-print-file-name=lib%s.so' % name],
                             capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return False
    path = (out.stdout or '').strip()
    return bool(path) and path != 'lib%s.so' % name and Path(path).exists()


def boost_libs(cxx=None):
    """Библиотеки для линковки: базовый набор плюс boost_system, только если она существует."""
    libs = list(BASE)
    if have_lib('boost_system', cxx):
        libs.insert(libs.index(_INSERT_AFTER) + 1, 'boost_system')
    return libs
