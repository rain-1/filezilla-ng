#include <filezilla.h>

#include "iothread.h"

#include <libfilezilla/file.hpp>

#include <assert.h>

CIOThread::CIOThread()
{
	m_buffers[0] = new char[BUFFERSIZE*BUFFERCOUNT];
	for (unsigned int i = 0; i < BUFFERCOUNT; ++i) {
		m_buffers[i] = m_buffers[0] + BUFFERSIZE * i;
		m_bufferLens[i] = 0;
	}
}

CIOThread::~CIOThread()
{
	Destroy();

	Close();

	delete [] m_buffers[0];
}

void CIOThread::Close()
{
	if (m_pFile) {
		// The file might have been preallocated and the transfer stopped before being completed
		// so always truncate the file to the actually written size before closing it.
		if (!m_read) {
			m_pFile->truncate();
		}

		m_pFile.reset();
	}
}

bool CIOThread::Create(fz::thread_pool& pool, std::unique_ptr<fz::file> && pFile, bool read, bool binary)
{
	assert(pFile);

	Close();

	m_pFile = std::move(pFile);
	m_read = read;
	m_binary = binary;

	if (read) {
		m_curAppBuf = BUFFERCOUNT - 1;
		m_curThreadBuf = 0;
	}
	else {
		m_curAppBuf = -1;
		m_curThreadBuf = 0;
	}

#ifdef SIMULATE_IO
	size_ = m_pFile->size();
#endif

	m_running = true;

	thread_ = pool.spawn([this]() { entry(); });
	if (!thread_) {
		m_running = false;
		return false;
	}

	return true;
}

void CIOThread::entry()
{
	if (m_read) {
		fz::scoped_lock l(m_mutex);
		while (m_running) {

			l.unlock();
			auto len = ReadFromFile(m_buffers[m_curThreadBuf], BUFFERSIZE);
			l.lock();

			if (m_appWaiting) {
				if (!m_evtHandler) {
					m_running = false;
					break;
				}
				m_appWaiting = false;
				m_evtHandler->send_event<CIOThreadEvent>();
			}

			if (len <= -1) {
				m_error = true;
				m_running = false;
				break;
			}

			m_bufferLens[m_curThreadBuf] = static_cast<unsigned int>(len);

			if (!len) {
				m_running = false;
				break;
			}

			++m_curThreadBuf %= BUFFERCOUNT;
			if (m_curThreadBuf == m_curAppBuf) {
				if (!m_running) {
					break;
				}

				m_threadWaiting = true;
				if (m_running) {
					m_condition.wait(l);
				}
			}
		}
	}
	else {
		fz::scoped_lock l(m_mutex);
		while (m_curAppBuf == -1) {
			if (!m_running) {
				return;
			}
			else {
				m_threadWaiting = true;
				m_condition.wait(l);
			}
		}

		for (;;) {
			while (m_curThreadBuf == m_curAppBuf) {
				if (!m_running) {
					return;
				}
				m_threadWaiting = true;
				m_condition.wait(l);
			}

			l.unlock();
			bool writeSuccessful = WriteToFile(m_buffers[m_curThreadBuf], BUFFERSIZE);
			l.lock();

			if (!writeSuccessful) {
				m_error = true;
				m_running = false;
			}

			if (m_appWaiting) {
				if (!m_evtHandler) {
					m_running = false;
					break;
				}
				m_appWaiting = false;
				m_evtHandler->send_event<CIOThreadEvent>();
			}

			if (m_error) {
				break;
			}

			++m_curThreadBuf %= BUFFERCOUNT;
		}
	}
}

int CIOThread::GetNextWriteBuffer(char** pBuffer)
{
	fz::scoped_lock l(m_mutex);

	if (m_error) {
		return IO_Error;
	}

	if (m_curAppBuf == -1) {
		m_curAppBuf = 0;
		*pBuffer = m_buffers[0];
		return IO_Success;
	}

	int newBuf = (m_curAppBuf + 1) % BUFFERCOUNT;
	if (newBuf == m_curThreadBuf) {
		m_appWaiting = true;
		return IO_Again;
	}

	if (m_threadWaiting) {
		m_condition.signal(l);
		m_threadWaiting = false;
	}

	m_curAppBuf = newBuf;
	*pBuffer = m_buffers[newBuf];

	return IO_Success;
}

bool CIOThread::Finalize(int len)
{
	assert(m_pFile);

	Destroy();

	if (m_curAppBuf == -1) {
		return true;
	}

	if (m_error) {
		return false;
	}

	if (!len) {
		return true;
	}

	if (!WriteToFile(m_buffers[m_curAppBuf], len)) {
		return false;
	}

#ifndef FZ_WINDOWS
	if (!m_binary && m_wasCarriageReturn) {
		const char CR = '\r';
		if (m_pFile->write(&CR, 1) != 1) {
			return false;
		}
	}
#endif

	m_curAppBuf = -1;

	return true;
}

int CIOThread::GetNextReadBuffer(char** pBuffer)
{
	assert(m_read);

	int newBuf = (m_curAppBuf + 1) % BUFFERCOUNT;

	fz::scoped_lock l(m_mutex);

	if (newBuf == m_curThreadBuf) {
		if (m_error) {
			return IO_Error;
		}
		else if (!m_running) {
			return IO_Success;
		}
		else {
			m_appWaiting = true;
			return IO_Again;
		}
	}

	if (m_threadWaiting) {
		m_condition.signal(l);
		m_threadWaiting = false;
	}

	*pBuffer = m_buffers[newBuf];
	m_curAppBuf = newBuf;

	return m_bufferLens[newBuf];
}

void CIOThread::Destroy()
{
	{
		fz::scoped_lock l(m_mutex);
		if (m_running) {
			m_running = false;
			if (m_threadWaiting) {
				m_threadWaiting = false;
				m_condition.signal(l);
			}
		}
	}

	thread_.join();
}

int64_t CIOThread::ReadFromFile(char* pBuffer, int64_t maxLen)
{
#ifdef SIMULATE_IO
	if (size_ < 0) {
		return 0;
	}
	size_ -= maxLen;
	return maxLen;
#endif

	// In binary mode, no conversion has to be done.
	// Also, under Windows the native newline format is already identical
	// to the newline format of the FTP protocol
#ifndef FZ_WINDOWS
	if (m_binary)
#endif
	{
		return m_pFile->read(pBuffer, maxLen);
	}

#ifndef FZ_WINDOWS

	// In the worst case, length will doubled: If reading
	// only LFs from the file
	const int readLen = maxLen / 2;

	char* r = pBuffer + readLen;
	auto len = m_pFile->read(r, readLen);
	if (!len || len <= -1) {
		return len;
	}

	const char* const end = r + len;
	char* w = pBuffer;

	// Convert all stand-alone LFs into CRLF pairs.
	while (r != end) {
		char c = *r++;
		if (c == '\n') {
			if (!m_wasCarriageReturn) {
				*w++ = '\r';
			}
			m_wasCarriageReturn = false;
		}
		else if (c == '\r') {
			m_wasCarriageReturn = true;
		}
		else {
			m_wasCarriageReturn = false;
		}

		*w++ = c;
	}

	return w - pBuffer;
#endif
}

bool CIOThread::WriteToFile(char* pBuffer, int64_t len)
{
#ifdef SIMULATE_IO
	return true;
#endif
	// In binary mode, no conversion has to be done.
	// Also, under Windows the native newline format is already identical
	// to the newline format of the FTP protocol
#ifndef FZ_WINDOWS
	if (m_binary) {
#endif
		return DoWrite(pBuffer, len);
#ifndef FZ_WINDOWS
	}
	else {

		// On all CRLF pairs, omit the CR. Don't harm stand-alone CRs

		// Handle trailing CR from last write
		if (m_wasCarriageReturn && len && *pBuffer != '\n' && *pBuffer != '\r') {
			m_wasCarriageReturn = false;
			const char CR = '\r';
			if (!DoWrite(&CR, 1)) {
				return false;
			}
		}

		// Skip forward to end of buffer or first CR
		const char* r = pBuffer;
		const char* const end = pBuffer + len;
		while (r != end && *r != '\r') {
			++r;
		}

		if (r != end) {
			// Now we gotta move data and also handle additional CRs.
			m_wasCarriageReturn = true;

			char* w = const_cast<char*>(r++);
			for (; r != end; ++r) {
				if (*r == '\r') {
					m_wasCarriageReturn = true;
				}
				else if (*r == '\n') {
					m_wasCarriageReturn = false;
					*(w++) = *r;
				}
				else {
					if (m_wasCarriageReturn) {
						m_wasCarriageReturn = false;
						*(w++) = '\r';
					}
					*(w++) = *r;
				}
			}
			len = w - pBuffer;
		}
		return DoWrite(pBuffer, len);
	}
#endif
}

bool CIOThread::DoWrite(const char* pBuffer, int64_t len)
{
	auto written = m_pFile->write(pBuffer, len);
	if (written == len) {
		return true;
	}

	auto err = GetSystemErrorCode();

	std::wstring const error = fz::to_wstring(GetSystemErrorDescription(err));

	fz::scoped_lock locker(m_mutex);
	m_error_description = error;

	return false;
}

std::wstring CIOThread::GetError()
{
	fz::scoped_lock locker(m_mutex);
	return m_error_description;
}

void CIOThread::SetEventHandler(fz::event_handler* handler)
{
	fz::scoped_lock locker(m_mutex);
	m_evtHandler = handler;
}
