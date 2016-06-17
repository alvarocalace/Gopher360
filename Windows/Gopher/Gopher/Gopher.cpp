#include "Gopher.h"

void inputKeyboard(WORD cmd, DWORD flag)
{
	INPUT input;
	input.type = INPUT_KEYBOARD;
	input.ki.wScan = 0;
	input.ki.time = 0;
	input.ki.dwExtraInfo = 0;
	input.ki.wVk = cmd;
	input.ki.dwFlags = flag;
	SendInput(1, &input, sizeof(INPUT));
}

void inputKeyboardDown(WORD cmd)
{
	inputKeyboard(cmd, 0);
}

void inputKeyboardUp(WORD cmd)
{
	inputKeyboard(cmd, KEYEVENTF_KEYUP);
}

void mouseEvent(WORD dwFlags, DWORD mouseData = 0)
{
	INPUT input;
	input.type = INPUT_MOUSE;
	input.mi.mouseData = 0;
	input.mi.dwFlags = dwFlags;
	input.mi.time = 0;
	SendInput(1, &input, sizeof(INPUT));
}

Gopher::Gopher(CXBOXController * controller)
	: _controller(controller)
{
	setupPowerOffCallback();
	setupAudioDeviceIds();
}

Gopher::~Gopher()
{
	if (SUCCEEDED(_hXInputDll))
	{
		FreeLibrary(_hXInputDll);
	}

	if (SUCCEEDED(_pPolicyConfig))
	{
		_pPolicyConfig->Release();
	}
}

void Gopher::loop() {
	Sleep(SLEEP_AMOUNT);

	_currentState = _controller->GetState();

	handleDisableButton();

	if (_disabled || handlePowerOff())
	{
		return;
	}

	handleMouseMovement();
	handleScrolling();
	handleAudioDeviceChange();

	mapMouseClick(XINPUT_GAMEPAD_A, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
	mapMouseClick(XINPUT_GAMEPAD_X, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
	mapMouseClick(XINPUT_GAMEPAD_LEFT_THUMB, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP);

	mapKeyboard(XINPUT_GAMEPAD_DPAD_UP, VK_UP);
	mapKeyboard(XINPUT_GAMEPAD_DPAD_DOWN, VK_DOWN);
	mapKeyboard(XINPUT_GAMEPAD_DPAD_LEFT, VK_LEFT);
	mapKeyboard(XINPUT_GAMEPAD_DPAD_RIGHT, VK_RIGHT);

	setXboxClickState(XINPUT_GAMEPAD_Y);
	if (_xboxClickIsDown[XINPUT_GAMEPAD_Y])
	{
		toggleWindowVisibility();
	}

	mapKeyboard(XINPUT_GAMEPAD_START, VK_LWIN);
	mapKeyboard(XINPUT_GAMEPAD_B, VK_RETURN);

	setXboxClickState(XINPUT_GAMEPAD_LEFT_SHOULDER);

	if (_xboxClickIsDown[XINPUT_GAMEPAD_LEFT_SHOULDER]) {
		speed = (speed == SPEED_SUPER_LOW) ? SPEED_MED : SPEED_SUPER_LOW;
	}
}

void Gopher::handleDisableButton()
{
	setXboxClickState(XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START);
	if (_xboxClickIsDown[XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START])
	{
		_disabled = !_disabled;

		if (_disabled) {
			Beep(1000, 300);
		}
		else
		{
			Beep(1800, 300);
		}
	}
}

void Gopher::handleAudioDeviceChange()
{
	setXboxClickState(XINPUT_GAMEPAD_RIGHT_SHOULDER);
	if (_xboxClickIsDown[XINPUT_GAMEPAD_RIGHT_SHOULDER])
	{
		changeToNextAudioDevice();
	}
}

void Gopher::toggleWindowVisibility()
{
	// if there's a hidden window, hide it
	if (_hiddenWindow != NULL)
	{
		WINDOWPLACEMENT windowPlacement;
		GetWindowPlacement(_hiddenWindow, &windowPlacement);

		// check that the window is still minimized
		if (windowPlacement.showCmd == SW_SHOWMINIMIZED)
		{
			ShowWindow(_hiddenWindow, SW_RESTORE);
		}
		_hiddenWindow = NULL;
	}
	else
	{
		if ((_hiddenWindow = GetForegroundWindow()) != NULL)
		{
			ShowWindow(_hiddenWindow, SW_FORCEMINIMIZE);
		}
	}
}

template <typename T>
int sgn(T val)
{
	return (T(0) < val) - (val < T(0));
}

float Gopher::getDelta(short t)
{
	//filter non-32768 and 32767, wireless ones can glitch sometimes and send it to the edge of the screen, it'll toss out some HUGE integer even when it's centered
	if (t > 32767) t = 0;
	if (t < -32768) t = 0;

	float delta = 0.0;

	if (abs(t) > DEAD_ZONE)
	{
		t = sgn(t) * (abs(t) - DEAD_ZONE);
		delta = speed * t / FPS;
	}

	return delta;
}

void Gopher::handleMouseMovement()
{
	POINT cursor;
	GetCursorPos(&cursor);

	short tx = _currentState.Gamepad.sThumbLX;
	short ty = _currentState.Gamepad.sThumbLY;

	float x = cursor.x + _xRest;
	float y = cursor.y + _yRest;

	float dx = getDelta(tx);
	float dy = getDelta(ty);

	x += dx;
	_xRest = x - (float)((int)x);

	y -= dy;
	_yRest = y - (float)((int)y);

	SetCursorPos((int)x, (int)y); //after all click input processing
}

void Gopher::handleScrolling()
{
	bool holdScrollUp = _currentState.Gamepad.sThumbRY > SCROLL_DEAD_ZONE;
	bool holdScrollDown = _currentState.Gamepad.sThumbRY < -SCROLL_DEAD_ZONE;

	if (holdScrollUp)
	{
		INPUT input;
		input.type = INPUT_MOUSE;
		input.mi.mouseData = SCROLL_SPEED;
		input.mi.dwFlags = MOUSEEVENTF_WHEEL;
		input.mi.time = 0;
		SendInput(1, &input, sizeof(INPUT));
	}

	if (holdScrollDown)
	{
		INPUT input;
		input.type = INPUT_MOUSE;
		input.mi.mouseData = -SCROLL_SPEED;
		input.mi.dwFlags = MOUSEEVENTF_WHEEL;
		input.mi.time = 0;
		SendInput(1, &input, sizeof(INPUT));
	}
}

void Gopher::setXboxClickState(DWORD STATE)
{
	_xboxClickIsDown[STATE] = false;
	_xboxClickIsUp[STATE] = false;

	if (!this->xboxClickStateExists(STATE))
	{
		_xboxClickStateLastIteration[STATE] = false;
	}

	bool isDown = _currentState.Gamepad.wButtons == STATE;

	if (isDown && !_xboxClickStateLastIteration[STATE])
	{
		_xboxClickStateLastIteration[STATE] = true;
		_xboxClickIsDown[STATE] = true;
	}

	if (!isDown && _xboxClickStateLastIteration[STATE])
	{
		_xboxClickStateLastIteration[STATE] = false;
		_xboxClickIsUp[STATE] = true;
	}

	_xboxClickStateLastIteration[STATE] = isDown;
}

bool Gopher::xboxClickStateExists(DWORD xinput)
{
	auto it = _xboxClickStateLastIteration.find(xinput);
	if (it == _xboxClickStateLastIteration.end())
	{
		return false;
	}

	return true;
}

void Gopher::mapKeyboard(DWORD STATE, DWORD key)
{
	setXboxClickState(STATE);
	if (_xboxClickIsDown[STATE])
	{
		inputKeyboardDown(key);
	}

	if (_xboxClickIsUp[STATE])
	{
		inputKeyboardUp(key);
	}
}

void Gopher::mapMouseClick(DWORD STATE, DWORD keyDown, DWORD keyUp)
{
	setXboxClickState(STATE);
	if (_xboxClickIsDown[STATE])
	{
		mouseEvent(keyDown);
	}

	if (_xboxClickIsUp[STATE])
	{
		mouseEvent(keyUp);
	}
}

void Gopher::setupPowerOffCallback()
{
	if (SUCCEEDED(_hXInputDll = LoadLibraryA("XInput1_3.dll")))
	{
		_powerOffCallback = (XInputPowerOffController)GetProcAddress(_hXInputDll, (LPCSTR)103);
	}
	else
	{
		printf("\nWarning: Could not set up power off functionality.\n");
	}
}

void Gopher::setupAudioDeviceIds()
{
	HRESULT hr = CoInitialize(NULL);
	if (SUCCEEDED(hr))
	{
		hr = CoCreateInstance(__uuidof(CPolicyConfigVistaClient),
			NULL, CLSCTX_ALL, __uuidof(IPolicyConfigVista), (LPVOID *)&_pPolicyConfig);
		if (SUCCEEDED(hr))
		{
			IMMDeviceEnumerator *pEnum = NULL;
			// Create a multimedia device enumerator.
			hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
				CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
			if (SUCCEEDED(hr))
			{
				IMMDeviceCollection *pDevices;
				// Enumerate the output devices.
				hr = pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pDevices);
				if (SUCCEEDED(hr))
				{
					UINT count;
					pDevices->GetCount(&count);
					if (SUCCEEDED(hr))
					{
						for (int i = 0; i < count; i++)
						{
							IMMDevice *pDevice;
							hr = pDevices->Item(i, &pDevice);
							if (SUCCEEDED(hr))
							{
								LPWSTR wstrID = NULL;
								hr = pDevice->GetId(&wstrID);
								if (SUCCEEDED(hr))
								{
									_audioDeviceIds.push_back(wstrID);
								}
								pDevice->Release();
							}
						}
					}
					pDevices->Release();
				}

				// set current default audio device
				_currentAudioDeviceIndex = 0;
				IMMDevice *pDefaultDevice = NULL;
				hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultDevice);
				if (SUCCEEDED(hr))
				{
					LPWSTR defaultDeviceID;
					hr = pDefaultDevice->GetId(&defaultDeviceID);
					if (SUCCEEDED(hr))
					{
						for (int i = 0; i < _audioDeviceIds.size(); i++)
						{
							if (wcscmp(_audioDeviceIds[i], defaultDeviceID) == 0)
							{
								_currentAudioDeviceIndex = i;
								break;
							}
						}
					}
					pDefaultDevice->Release();
				}
			}
		}
	}
}

HRESULT Gopher::changeToNextAudioDevice()
{
	HRESULT hr = NULL;

	if (_audioDeviceIds.size() > 0 && SUCCEEDED(_pPolicyConfig))
	{
		_currentAudioDeviceIndex = (_currentAudioDeviceIndex + 1 >= _audioDeviceIds.size()) ? 0 : (_currentAudioDeviceIndex + 1);
		hr = _pPolicyConfig->SetDefaultEndpoint(_audioDeviceIds[_currentAudioDeviceIndex], eConsole);
	}

	return hr;
}

bool Gopher::handlePowerOff()
{
	bool poweredOff = false;

	if (SUCCEEDED(_hXInputDll) && _currentState.Gamepad.bRightTrigger == 0xFF)
	{
		Beep(120, 300);
		if (SUCCEEDED(_powerOffCallback(0)))
		{
			poweredOff = true;
		}
	}

	return poweredOff;
}