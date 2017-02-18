#include <filezilla.h>
#include "option_change_event_handler.h"

#include <algorithm>

std::vector<COptionChangeEventHandler*> COptionChangeEventHandler::m_handlers;
std::size_t COptionChangeEventHandler::notify_index_ = 0;

COptionChangeEventHandler::~COptionChangeEventHandler()
{
	UnregisterAllOptions();
}

void COptionChangeEventHandler::UnregisterAllOptions()
{
	if (m_handled_options.any()) {
		auto it = std::find(m_handlers.begin(), m_handlers.end(), this);
		if (it != m_handlers.end()) {
			m_handlers.erase(it);
		}
	}
}

void COptionChangeEventHandler::RegisterOption(int option)
{
	if (option < 0) {
		return;
	}

	if (m_handled_options.none()) {
		m_handlers.push_back(this);
	}
	m_handled_options.set(option);
}

void COptionChangeEventHandler::UnregisterOption(int option)
{
	m_handled_options.set(option, false);
	if (m_handled_options.none()) {
		auto it = std::find(m_handlers.begin(), m_handlers.end(), this);
		if (it != m_handlers.end()) {
			m_handlers.erase(it);

			// If this had been called in the context of DoNotify, make sure all handlers get called
			if (static_cast<std::size_t>(std::distance(m_handlers.begin(), it)) <= notify_index_) {
				--notify_index_;
			}
		}
	}
}

void COptionChangeEventHandler::UnregisterAllHandlers()
{
	for (auto & handler : m_handlers) {
		handler->m_handled_options.reset();
	}
	m_handlers.clear();
}

void COptionChangeEventHandler::DoNotify(changed_options_t const& options)
{
	// Going over notify_index_ which may be changed by UnregisterOption
	// Bit ugly but otherwise has reentrancy issues.
	for (notify_index_ = 0; notify_index_ < m_handlers.size(); ++notify_index_) {
		auto & handler = m_handlers[notify_index_];
		auto hoptions = options & handler->m_handled_options;
		if (hoptions.any()) {
			handler->OnOptionsChanged(hoptions);
		}
	}
}
