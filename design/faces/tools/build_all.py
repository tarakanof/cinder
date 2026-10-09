import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
GENERATORS = ['temp', 'ad', 'umbrella', 'music', 'wind', 'sun', 'air', 'days', 'bot2', 'gallery']

if __name__ == '__main__':
    for g in GENERATORS:
        r = subprocess.run([sys.executable, os.path.join(HERE, g + '.py')], capture_output=True, text=True)
        if r.returncode:
            sys.exit(f'{g}.py failed:\n{r.stderr}')
        print(f'{g}.py ok')
