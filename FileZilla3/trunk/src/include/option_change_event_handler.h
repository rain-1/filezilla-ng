#ifndef FILEZILLA_ENGINE_OPTION_CHANGE_EVENT_HANDLER_HEADER
#define FILEZILLA_ENGINE_OPTION_CHANGE_EVENT_HANDLER_HEADER

#include <bitset>
#include <set>
#include <vector>

class COptions;

enum {
	changed_options_size = 64*3
};

typedef std::bitset<changed_options_size> changed_options_t;

class COptionChangeEventHandler
{
	friend class COptions;

public:
	COptionChangeEventHandler() = default;
	virtual ~COptionChangeEventHandler();

	void RegisterOption(int option);
	void UnregisterOption(int option);
	void UnregisterAllOptions();

protected:
	virtual void OnOptionsChanged(changed_options_t const& options) = 0;

private:
	changed_options_t m_handled_options;

	// Very important: Never ever call this if there's OnOptionsChanged on the stack.
	static void DoNotify(changed_options_t const& options);
	static std::size_t notify_index_;

	static void UnregisterAllHandlers();

	static std::vector<COptionChangeEventHandler*> m_handlers;
};

#endif
