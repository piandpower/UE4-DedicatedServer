// Copyright 2004-2016 YaS-Online, Inc. All Rights Reserved.

#include "DedicatedServerPrivatePCH.h"

#define LOCTEXT_NAMESPACE "FServerConsole"

#if WITH_SERVER_CODE
	#if PLATFORM_WINDOWS
		#include "AllowWindowsPlatformTypes.h"

		namespace ConsoleConstants
		{
			uint32 WIN_ATTACH_PARENT_PROCESS = ATTACH_PARENT_PROCESS;
			uint32 WIN_STD_INPUT_HANDLE = STD_INPUT_HANDLE;
			uint32 WIN_STD_OUTPUT_HANDLE = STD_OUTPUT_HANDLE;
			uint32 WIN_STD_ERROR_HANDLE = STD_ERROR_HANDLE;
		}

		#include "HideWindowsPlatformTypes.h"
	#endif

	FServerConsole::FServerConsole()
	{
		m_pConsole = static_cast<FOutputDeviceConsolePlatform*>( GLogConsole );
		m_iCommandHistoryIndex = -1;

		#if PLATFORM_WINDOWS
			m_hOutputHandle = INVALID_HANDLE_VALUE;
			m_hInputHandle = INVALID_HANDLE_VALUE;
		#endif
	}

	FServerConsole::~FServerConsole()
	{
	}

	void FServerConsole::Show( bool bShowWindow )
	{
		if( !m_pConsole->IsShown() ) m_pConsole->Show( bShowWindow );

		#if PLATFORM_WINDOWS
			if( m_hOutputHandle == INVALID_HANDLE_VALUE )
			{
				m_hOutputHandle = ::GetStdHandle( ConsoleConstants::WIN_STD_OUTPUT_HANDLE );
				m_hInputHandle = ::GetStdHandle( ConsoleConstants::WIN_STD_INPUT_HANDLE );

				// Fix input mode as it defaults to 439, which is bigger then all possible flags combined...
				if( m_hInputHandle != INVALID_HANDLE_VALUE ) ::SetConsoleMode( m_hInputHandle, ENABLE_PROCESSED_INPUT );
			}
		#elif PLATFORM_MAC
		#elif PLATFORM_LINUX
		#else
			#error You shall not pass!
		#endif
	}

	bool FServerConsole::IsShown()
	{
		return m_pConsole->IsShown();
	}

	bool FServerConsole::IsAttached()
	{
		return m_pConsole->IsAttached();
	}

	void FServerConsole::Serialize( const TCHAR* sData, ELogVerbosity::Type eVerbosity, const class FName& sCategory, const double fTime )
	{
		FScopeLock hLock( &m_hLock );

		//ToDo: Save Cursor Position

		ClearInputLine();

		m_pConsole->Serialize( sData, eVerbosity, sCategory );

		RedrawInputLine();

		// ToDo: Restore CursorPosition
	}

	void FServerConsole::Serialize( const TCHAR* sData, ELogVerbosity::Type eVerbosity, const class FName& sCategory )
	{
		Serialize( sData, eVerbosity, sCategory, -1.0 );
	}

	#if PLATFORM_WINDOWS
		void FServerConsole::Tick()
		{
			if( m_hInputHandle != INVALID_HANDLE_VALUE )
			{
				unsigned long iEventsRead;
				INPUT_RECORD hInputEvent[1];

				if( m_hCachedInputEvent.EventType == KEY_EVENT )
				{
					hInputEvent[0] = m_hCachedInputEvent;

					if( m_hCachedInputEvent.Event.KeyEvent.wRepeatCount == 0 ) m_hCachedInputEvent.EventType = -1;
					else m_hCachedInputEvent.Event.KeyEvent.wRepeatCount--;
				}
				else if( ::ReadConsoleInput( m_hInputHandle, hInputEvent, 1, &iEventsRead ) && iEventsRead > 0 )
				{
					// ToDo: Alt+Numpad sequence: http://referencesource.microsoft.com/#mscorlib/system/console.cs,1512

					if( !hInputEvent[0].Event.KeyEvent.bKeyDown )
					{
						if( hInputEvent[0].Event.KeyEvent.wRepeatCount > 1 )
						{
							hInputEvent[0].Event.KeyEvent.wRepeatCount--;
							m_hCachedInputEvent = hInputEvent[0];
						}
					}
				}
				else return;// ToDo: fatal error

				if( hInputEvent[0].EventType == KEY_EVENT && hInputEvent[0].Event.KeyEvent.bKeyDown )
				{
					KEY_EVENT_RECORD hEvent = hInputEvent[0].Event.KeyEvent;

					if( hEvent.wVirtualKeyCode == VK_RETURN )
					{
						FScopeLock hLock( &m_hLock );

						ClearInputLine();

						m_pConsole->SetColor( COLOR_GREEN );

						TCHAR sOutput[MAX_SPRINTF] = TEXT( "" );
						unsigned long iCharsWritten;

						if( GEngine->Exec( GEngine->GetWorld(), *m_sInput ) )
						{
							FCString::Sprintf( sOutput, TEXT( "> %s%s" ), *m_sInput, LINE_TERMINATOR );
							::WriteConsole( m_hOutputHandle, sOutput, FCString::Strlen( sOutput ), &iCharsWritten, NULL );

							m_hCommandHistory.Add( m_sInput );
						}
						else
						{
							FCString::Sprintf( sOutput, TEXT( "Unkown Command: %s%s" ), *m_sInput, LINE_TERMINATOR );
							::WriteConsole( m_hOutputHandle, sOutput, FCString::Strlen( sOutput ), &iCharsWritten, NULL );
						}

						m_sInput.Empty();

						m_pConsole->SetColor( COLOR_NONE );
					}
					else if( hEvent.wVirtualKeyCode == VK_BACK )
					{
						FScopeLock hLock( &m_hLock );

						if( m_sInput.Len() >= 1 )
						{
							m_sInput.RemoveAt( m_sInput.Len() - 1 );

							RedrawInputLine();
						}
					}
					else if( hEvent.wVirtualKeyCode == VK_ESCAPE )
					{
						FScopeLock hLock( &m_hLock );

						ClearInputLine();
						m_sInput.Empty();
					}
					else if( hEvent.wVirtualKeyCode == VK_TAB )
					{
						FScopeLock hLock( &m_hLock );

						// ToDo: AutoCompletion
					}
					else if( hEvent.wVirtualKeyCode == VK_UP )
					{
						FScopeLock hLock( &m_hLock );

						if( m_hCommandHistory.Num() == 0 ) return;

						if( m_iCommandHistoryIndex == -1 ) m_iCommandHistoryIndex = m_hCommandHistory.Num() - 1;
						else --m_iCommandHistoryIndex;

						if( m_iCommandHistoryIndex < 0 ) m_iCommandHistoryIndex = 0;

						// ToDo: Save user input if present

						m_sInput = m_hCommandHistory[m_iCommandHistoryIndex];

						RedrawInputLine();
					}
					else if( hEvent.wVirtualKeyCode == VK_DOWN )
					{
						FScopeLock hLock( &m_hLock );

						if( m_iCommandHistoryIndex == -1 ) return;

						++m_iCommandHistoryIndex;

						if( m_iCommandHistoryIndex > m_hCommandHistory.Num() ) m_iCommandHistoryIndex = m_hCommandHistory.Num();

						if( m_iCommandHistoryIndex == m_hCommandHistory.Num() )
						{
							// ToDo: Restore user input if present
							m_sInput.Empty();

							RedrawInputLine();
						}
						else
						{
							m_sInput = m_hCommandHistory[m_iCommandHistoryIndex];

							RedrawInputLine();
						}
					}
					else if( hEvent.wVirtualKeyCode == VK_LEFT )
					{
						FScopeLock hLock( &m_hLock );

						COORD hCursorPosition( GetCursorPosition() );

						if( hCursorPosition.X > 0 )
						{
							--hCursorPosition.X;
							::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
						}
					}
					else if( hEvent.wVirtualKeyCode == VK_RIGHT )
					{
						FScopeLock hLock( &m_hLock );

						COORD hCursorPosition( GetCursorPosition() );

						if( hCursorPosition.X < m_sInput.Len() )
						{
							++hCursorPosition.X;
							::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
						}
					}
					else if( hEvent.wVirtualKeyCode == VK_HOME )
					{
						FScopeLock hLock( &m_hLock );

						COORD hCursorPosition( GetCursorPosition() );
						hCursorPosition.X = 0;
						::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
					}
					else if( hEvent.wVirtualKeyCode == VK_END )
					{
						FScopeLock hLock( &m_hLock );

						COORD hCursorPosition( GetCursorPosition() );
						hCursorPosition.X = m_sInput.Len();
						::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
					}
					else if( hEvent.wVirtualKeyCode == VK_DELETE )
					{
						FScopeLock hLock( &m_hLock );

						COORD hCursorPosition( GetCursorPosition() );

						if( hCursorPosition.X <= m_sInput.Len() )
						{
							m_sInput.RemoveAt( hCursorPosition.X );
							m_sInput.AppendChar( ' ' );

							RedrawInputLine();

							m_sInput.RemoveAt( m_sInput.Len() - 1 );

							::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
						}
					}
					else if( hEvent.uChar.AsciiChar != '\0' )
					{
						FScopeLock hLock( &m_hLock );

						m_sInput.AppendChar( hEvent.uChar.AsciiChar );

						RedrawInputLine();
					}
				}
			}
		}

		void FServerConsole::ClearInputLine()
		{
			FScopeLock hLock( &m_hLock );

			COORD hCursorPosition( GetCursorPosition() );

			if( hCursorPosition.X == 0 ) return;

			TCHAR sOutput[MAX_SPRINTF] = TEXT( "" );
			for( int i = 0; i <= hCursorPosition.X; i++ ) sOutput[i] = ' ';

			hCursorPosition.X = 0;
			::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );

			m_pConsole->SetColor( COLOR_NONE );

			unsigned long iCharsWritten;
			::WriteConsole( m_hOutputHandle, sOutput, FCString::Strlen( sOutput ), &iCharsWritten, NULL );

			hCursorPosition.X = 0;
			::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
		}

		void FServerConsole::RedrawInputLine()
		{
			ClearInputLine();

			if( m_hOutputHandle != INVALID_HANDLE_VALUE && !m_sInput.IsEmpty() )
			{
				FScopeLock hLock( &m_hLock );

				m_pConsole->SetColor( COLOR_GREEN );

				unsigned long iCharsWritten;
				::WriteConsole( m_hOutputHandle, *m_sInput, m_sInput.Len(), &iCharsWritten, NULL );
			}
		}

		COORD FServerConsole::GetCursorPosition()
		{
			COORD hCursorPosition;

			if( m_hOutputHandle != INVALID_HANDLE_VALUE )
			{
				CONSOLE_SCREEN_BUFFER_INFO hConsoleInfo;

				if( ::GetConsoleScreenBufferInfo( m_hOutputHandle, &hConsoleInfo ) ) hCursorPosition = hConsoleInfo.dwCursorPosition;
			}

			return hCursorPosition;
		}
	#elif PLATFORM_MAC
		void FServerConsole::Tick()
		{
		}

		void FServerConsole::ClearInputLine()
		{
		}

		void FServerConsole::RedrawInputLine()
		{
		}
	#elif PLATFORM_LINUX
		void FServerConsole::Tick()
		{
		}

		void FServerConsole::ClearInputLine()
		{
		}

		void FServerConsole::RedrawInputLine()
		{
		}
	#else
		#error Nope
	#endif
#endif

#undef LOCTEXT_NAMESPACE
