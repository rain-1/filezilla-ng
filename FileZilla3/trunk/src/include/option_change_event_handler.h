#ifndef __OPTION_CHANGE_EVENT_HANDLER_H__
#define __OPTION_CHANGE_EVENT_HANDLER_H__

#include <set>
#include <vector>

class COptions;

class COptionChangeEventHandler
{
	friend class COptions;

public:
	COptionChangeEventHandler();
	virtual ~COptionChangeEventHandler();

	void RegisterOption(int option);
	void UnregisterOption(int option);

protected:
	virtual void OnOptionChanged(int option) = 0;

private:
	std::set<int> m_handled_options;

	static void DoNotify(std::vector<unsigned int> && options);
	static void UnregisterAll();

	static std::vector<std::vector<COptionChangeEventHandler*> > m_handlers;
};

#endif //__OPTION_CHANGE_EVENT_HANDLER_H__
