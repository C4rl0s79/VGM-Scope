@echo off
if not exist .venv (
  py -3.11 -m venv .venv
)
call .venv\Scripts\activate.bat
pip install -r requirements.txt
python -m app.main
pause
