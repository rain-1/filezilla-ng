#include <filezilla.h>
#include "option_change_event_handler.h"

#include <algorithm>

std::vector<std::vector<COptionChangeEventHandler*> > COptionChangeEventHandler::m_handlers;

COptionChangeEventHandler::COptionChangeEventHandler()
{
}

COptionChangeEventHandler::~COptionChangeEventHandler()
{
	for (auto const& option : m_handled_options) {
		auto it = std::find(m_handlers[option].begin(), m_handlers[option].end(), this);
		if (it != m_handlers[option].end()) {
			m_handlers[option].erase(it);
		}
	}
}

void COptionChangeEventHandler::RegisterOption(int option)
{
	if (option < 0 )
		return;

	while (static_cast<size_t>(option) >= m_handlers.size())
		m_handlers.push_back(std::vector<COptionChangeEventHandler*>());

	m_handled_options.insert(option);
	if (std::find(m_handlers[option].begin(), m_handlers[option].end(), this) == m_handlers[option].end()) {
		m_handlers[option].push_back(this);
	}
}

void COptionChangeEventHandler::UnregisterOption(int option)
{
	if (m_handled_options.erase(option)) {
		auto it = std::find(m_handlers[option].begin(), m_handlers[option].end(), this);
		if (it != m_handlers[option].end()) {
			m_handlers[option].erase(it);
		}
	}
}

void COptionChangeEventHandler::UnregisterAll()
{
	for (auto & handler : m_handlers) {
		for (auto & iter : handler) {
			iter->m_handled_options.clear();
		}
		handler.clear();
	}
}

void COptionChangeEventHandler::DoNotify(std::vector<unsigned int> && options)
{
	for (unsigned int option : options) {
		if (option < 0 || static_cast<size_t>(option) >= m_handlers.size())
			return;

		for (auto & handler : m_handlers[option]) {
			handler->OnOptionChanged(option);
		}
	}
}
