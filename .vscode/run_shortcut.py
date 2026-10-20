"""Select and run a Pintos test for the project supplied by VS Code settings."""
import re
import shlex
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def test_names(project):
    # Ask make to expand its own test definitions, including nested test groups.
    makefile = (
        f'include {project}/Make.vars\n'
        'include $(addsuffix /Make.tests,$(TEST_SUBDIRS))\n'
        '.PHONY: vscode-list-tests\n'
        'vscode-list-tests:\n'
        '\t@echo $(foreach dir,$(TEST_SUBDIRS),$($(dir)_TESTS))\n'
    )
    result = subprocess.run(
        ['make', '--no-print-directory', '-s', '-f', '-', 'vscode-list-tests'],
        cwd=ROOT / 'pintos', input=makefile, text=True,
        stdout=subprocess.PIPE, check=True,
    )
    return sorted(set(name for name in result.stdout.split()
                      if name.startswith(f'tests/{project}/')))


def resolve_project(value):
    config = json.loads((ROOT / '.vscode/settings.json').read_text())
    projects = config['pintos.projects']
    if not isinstance(projects, list) or not projects or any(
            name not in ('threads', 'userprog', 'vm', 'ALL') for name in projects):
        raise ValueError('pintos.projects must list threads, userprog, vm, or ALL')
    if not value.isdigit() or int(value) >= len(projects):
        raise ValueError(f'pintos.project must be an index from 0 to {len(projects) - 1}')
    return projects[int(value)]


def build(project):
    return subprocess.run(['make', '-C', str(ROOT / 'pintos' / project), '-j4']).returncode


def run_make(project, targets):
    env = os.environ.copy()
    env['PATH'] = str(ROOT / 'pintos/utils') + os.pathsep + env.get('PATH', '')
    command = ['make', '-k', '-C', str(ROOT / 'pintos' / project / 'build'),
               '-W', 'os.dsk', *targets, 'VERBOSE=1']
    with subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True) as process:
        for line in process.stdout:
            print(re.sub(r'\b(PASS|FAIL)\b', color_result, line,
                         flags=re.IGNORECASE), end='', flush=True)
        return process.wait()


def run_all():
    results = {}
    for project in ('threads', 'userprog', 'vm'):
        print(f'\nALL: {project} 전체 테스트 실행', flush=True)
        results[project] = build(project) or run_make(project, ['check'])
    print('\n전체 실행 결과:', flush=True)
    for project, status in results.items():
        print(f'{project}: {"PASS" if status == 0 else "FAIL (테스트 또는 빌드 오류)"}')
    return int(any(results.values()))


def select_test(project, shortcut, allow_all=False, allow_range=False):
    tests = test_names(project)
    if not tests:
        raise ValueError(f'No tests found for {project}')
    print(f'\n{shortcut}: {project} 테스트 선택', flush=True)
    for number, test in enumerate(tests, 1):
        print(f'{number:3}. {test.removeprefix("tests/" + project + "/")}')
    if allow_all:
        print(f'{len(tests) + 1:3}. 현재 프로젝트 전체 테스트 실행 (all)')
    prompt = '\n테스트 번호 또는 이름'
    if allow_range:
        prompt += ', 범위 (예: 1..7)'
    prompt += ' (q: 취소): '
    while True:
        mode = '<<디버그 모드>>' if shortcut == 'F5' else '<<실행 모드>>'
        print(f'\n======= {mode} =======')
        choice = input(prompt).strip()
        if choice.lower() == 'q':
            return None
        if allow_all and (choice.lower() == 'all' or choice == str(len(tests) + 1)):
            return 'all'
        if allow_range and '..' in choice:
            match = re.fullmatch(r'(\d+)\s*\.\.\s*(\d+)', choice)
            if match:
                start, end = map(int, match.groups())
                if 1 <= start <= end <= len(tests):
                    return tests[start - 1:end]
            print(f'범위는 1..{len(tests)} 안에서 시작 번호 <= 끝 번호로 입력하세요. '
                  '전체 실행 항목은 범위에 포함할 수 없습니다.')
            continue
        if choice.isdigit() and 1 <= int(choice) <= len(tests):
            return tests[int(choice) - 1]
        selected = choice if choice.startswith('tests/') else f'tests/{project}/{choice}'
        if selected in tests:
            return selected
        print('목록의 번호 또는 이름을 입력하세요.')


def debug_arguments(project, selected):
    # Dry-run the real test recipe: make supplies arguments, disks and PUTFILES.
    result = subprocess.run(
        ['make', '--no-print-directory', '-n', '-W', 'os.dsk',
         selected + '.output'], cwd=ROOT / 'pintos' / project / 'build',
        text=True, stdout=subprocess.PIPE, check=True)
    commands = [line for line in result.stdout.splitlines()
                if line.startswith('pintos ')]
    if len(commands) != 1:
        raise ValueError('선택한 테스트의 Pintos 실행 명령을 찾을 수 없습니다.')
    tokens = shlex.split(commands[0])
    # Stop before the test runner's output redirection; keep quoted run arguments.
    arguments = tokens[1:tokens.index('<')]
    # A test timeout must not kill QEMU while stopped at a breakpoint.
    index = arguments.index('-T')
    del arguments[index:index + 2]
    return arguments


def run_range(project, tests):
    results = {'PASS': [], 'FAIL': [], 'ERROR': []}
    for test in tests:
        print(f'\n실행: {test}', flush=True)
        result = ROOT / 'pintos' / project / 'build' / (test + '.result')
        try:
            # Never mistake an earlier run's verdict for this run's result.
            result.unlink(missing_ok=True)
            status = run_make(project, [test + '.result'])
            lines = result.read_text().splitlines() if result.exists() else []
            verdict = lines[0].strip() if lines else ''
            if verdict == 'FAIL':
                results['FAIL'].append(test)
            elif status == 0 and verdict == 'PASS':
                results['PASS'].append(test)
            else:
                results['ERROR'].append(test)
                print(f'ERROR: {test} (종료 코드 {status}, 유효한 판정 결과 없음)')
        except OSError as error:
            results['ERROR'].append(test)
            print(f'ERROR: {test}: {error}')

    print(f'\n구간 실행 결과: 총 {len(tests)}개')
    print(' | '.join(f'{verdict}: {len(names)}개'
                     for verdict, names in results.items()))
    for verdict in ('FAIL', 'ERROR'):
        if results[verdict]:
            print(f'\n{verdict} 테스트:')
            for test in results[verdict]:
                print(f'  - {test}')
    return int(bool(results['FAIL'] or results['ERROR']))


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ('test', 'build', 'debug-build'):
        raise ValueError('Usage: run_shortcut.py <test|build|debug-build> <project index>')
    action, value = sys.argv[1:]
    project = resolve_project(value)
    if action == 'debug-build':
        if project == 'ALL':
            raise ValueError('F5는 ALL을 지원하지 않습니다. pintos.project를 0~2로 지정하세요.')
        state = ROOT / 'pintos' / project / 'build' / '.vscode-debug.json'
        state.unlink(missing_ok=True)
        selected = select_test(project, 'F5')
        if selected is None:
            return 130
        status = build(project)
        if status:
            return status
        link = ROOT / 'pintos/.debug-build'
        if link.is_symlink():
            link.unlink()
        link.symlink_to(Path(project) / 'build', target_is_directory=True)
        arguments = debug_arguments(project, selected)
        state.write_text(json.dumps({'project': project, 'test': selected,
                                     'arguments': arguments}))
        print(f'디버깅: {selected}', flush=True)
        return 0
    if action == 'build':
        projects = ('threads', 'userprog', 'vm') if project == 'ALL' else (project,)
        return int(any([build(name) for name in projects]))
    if project == 'ALL':
        return run_all()
    selected = select_test(project, 'F6', allow_all=True, allow_range=True)
    if selected is None:
        return 0
    label = f'{project} 전체 테스트' if selected == 'all' else selected
    if isinstance(selected, list):
        label = f'{len(selected)}개 테스트: ' + ', '.join(selected)
    print(f'\n실행: {label}', flush=True)
    status = build(project)
    if status:
        return status
    if selected == 'all':
        return run_make(project, ['check'])
    if isinstance(selected, list):
        return run_range(project, selected)
    status = run_make(project, [selected + '.result'])
    result = ROOT / 'pintos' / project / 'build' / (selected + '.result')
    return status or int(not result.exists() or result.read_text().strip() != 'PASS')


def color_result(match):
    color = '32' if match.group().upper() == 'PASS' else '31'
    return f'\033[{color}m{match.group()}\033[0m'



if __name__ == '__main__':
    try:
        sys.exit(main())
    except (KeyboardInterrupt, EOFError):
        print('\n테스트 취소.')
        sys.exit(130)
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'Pintos 오류: {error}', file=sys.stderr)
        sys.exit(1)
