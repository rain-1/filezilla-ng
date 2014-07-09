#ifndef __REFCOUNT_H__
#define __REFCOUNT_H__

#include <memory>
#include <type_traits>

template<class T> struct CRefcountObjectData final
{
	inline T* get() {
		return static_cast<T*>(static_cast<void*>(&v_));
	}

	inline T const* get() const {
		return static_cast<T const*>(static_cast<void const*>(&v_));
	}

	typename std::aligned_storage<sizeof(T)>::type v_;
	int refcount_;
};

// Trivial template class to refcount objects with COW semantics.
template<class T> class CRefcountObject final
{
public:
	CRefcountObject();
	CRefcountObject(const CRefcountObject<T>& v);
	CRefcountObject(const T& v);
	~CRefcountObject();

	void clear();

	T& Get();

	const T& operator*() const;
	const T* operator->() const;

	bool operator==(const CRefcountObject<T>& cmp) const;
	inline bool operator!=(const CRefcountObject<T>& cmp) const { return !(*this == cmp); }
	bool operator<(const CRefcountObject<T>& cmp) const;

	CRefcountObject<T>& operator=(const CRefcountObject<T>& v);
protected:
	CRefcountObjectData<T> *data_;
};

template<class T> class CRefcountObject_Uninitialized final
{
	/* Almost same as CRefcountObject but does not allocate
	   an object initially.
	   You need to ensure to assign some data prior to calling
	   operator* or ->, otherwise you'll dereference the null-pointer.
	 */
public:
	CRefcountObject_Uninitialized();
	CRefcountObject_Uninitialized(const CRefcountObject_Uninitialized<T>& v);
	CRefcountObject_Uninitialized(const T& v);
	~CRefcountObject_Uninitialized();

	void clear();

	T& Get();

	const T& operator*() const;
	const T* operator->() const;

	bool operator==(const CRefcountObject_Uninitialized<T>& cmp) const;
	inline bool operator!=(const CRefcountObject_Uninitialized<T>& cmp) const { return !(*this == cmp); }
	bool operator<(const CRefcountObject_Uninitialized<T>& cmp) const;

	CRefcountObject_Uninitialized<T>& operator=(const CRefcountObject_Uninitialized<T>& v);

	bool operator!() const { return data_ == 0; }
protected:
	CRefcountObjectData<T> *data_;
};

template<class T> bool CRefcountObject<T>::operator==(const CRefcountObject<T>& cmp) const
{
	if (data_ == cmp.data_)
		return true;

	return *data_->get() == *cmp.data_->get();
}

template<class T> CRefcountObject<T>::CRefcountObject()
{
	data_ = new CRefcountObjectData<T>;
	data_->refcount_ = 1;
	new(&data_->v_) T;
}

template<class T> CRefcountObject<T>::CRefcountObject(const CRefcountObject<T>& v)
{
	data_ = v.data_;
	++(data_->refcount_);
}

template<class T> CRefcountObject<T>::CRefcountObject(const T& v)
{
	data_ = new CRefcountObjectData<T>;
	data_->refcount_ = 1;
	new(&data_->v_) T(v);
}

template<class T> CRefcountObject<T>::~CRefcountObject()
{
	if (--(data_->refcount_) == 0) {
		data_->get()->~T();
		delete data_;
	}
}

template<class T> T& CRefcountObject<T>::Get()
{
	if (data_->refcount_ != 1) {
		--(data_->refcount_);
		
		CRefcountObjectData<T> *data = new CRefcountObjectData<T>;
		data->refcount_ = 1;
		new (&data->v_) T(*data_->get());
		data_ = data;
	}

	return *data_->get();
}

template<class T> CRefcountObject<T>& CRefcountObject<T>::operator=(const CRefcountObject<T>& v)
{
	if (data_ == v.data_)
		return *this;
	if (--(data_->refcount_) == 0) {
		data_->get()->~T();
		delete data_;
	}

	data_ = v.data_;
	++(data_->refcount_);
	return *this;
}

template<class T> bool CRefcountObject<T>::operator<(const CRefcountObject<T>& cmp) const
{
	if (data_ == cmp.data_)
		return false;

	return *data_->get() < *cmp.data_->get();
}

template<class T> void CRefcountObject<T>::clear()
{
	if (data_->refcount_ != 1) {
		--(data_->refcount_);
		data_ = new CRefcountObjectData<T>;
		data_->refcount_ = 1;
	}
	else {
		data_->get()->~T();
	}
	new (&data_->v_) T;
}

template<class T> const T& CRefcountObject<T>::operator*() const
{
	return *data_->get();
}

template<class T> const T* CRefcountObject<T>::operator->() const
{
	return data_->get();
}

// The same for the uninitialized version
template<class T> bool CRefcountObject_Uninitialized<T>::operator==(const CRefcountObject_Uninitialized<T>& cmp) const
{
	if (data_ == cmp.data_)
		return true;

	return *data_->get() == *data_->get();
}

template<class T> CRefcountObject_Uninitialized<T>::CRefcountObject_Uninitialized()
{
	data_ = 0;
}

template<class T> CRefcountObject_Uninitialized<T>::CRefcountObject_Uninitialized(const CRefcountObject_Uninitialized<T>& v)
{
	data_ = v.data_;
	if( data_ ) {
		++(data_->refcount_);
	}
}

template<class T> CRefcountObject_Uninitialized<T>::CRefcountObject_Uninitialized(const T& v)
{
	data_ = new CRefcountObjectData<T>;
	data_->refcount_ = 1;
	new (&data_->v_) T(v);
}

template<class T> CRefcountObject_Uninitialized<T>::~CRefcountObject_Uninitialized()
{
	if (!data_)
		return;

	if (--(data_->refcount_) == 0 ) {
		data_->get()->~T();
		delete data_;
	}
}

template<class T> T& CRefcountObject_Uninitialized<T>::Get()
{
	if (!data_) {
		data_ = new CRefcountObjectData<T>;
		data_->refcount_ = 1;
		new (&data_->v_) T;
	}
	else if (data_->refcount_ != 1) {
		--(data_->refcount_);

		CRefcountObjectData<T> *data = new CRefcountObjectData<T>;
		data->refcount_ = 1;
		new (&data->v_) T(*data_->get());
		data_ = data;
	}

	return *data_->get();
}

template<class T> CRefcountObject_Uninitialized<T>& CRefcountObject_Uninitialized<T>::operator=(const CRefcountObject_Uninitialized<T>& v)
{
	if (data_ == v.data_)
		return *this;
	if (data_ && --(data_->refcount_) == 0) {
		data_->get()->~T();
		delete data_;
	}

	data_ = v.data_;
	if( data_ ) {
		++(data_->refcount_);
	}
	
	return *this;
}

template<class T> bool CRefcountObject_Uninitialized<T>::operator<(const CRefcountObject_Uninitialized<T>& cmp) const
{
	if (data_ == cmp.data_)
		return false;
	if (!data_)
		return true;
	if (!cmp.data_)
		return false;

	return *data_->get() < *cmp.data_->get();
}

template<class T> void CRefcountObject_Uninitialized<T>::clear()
{
	if (!data_)
		return;

	if (--(data_->refcount_) == 0) {
		data_->get()->~T();
		delete data_;
	}
	data_ = 0;
}

template<class T> const T& CRefcountObject_Uninitialized<T>::operator*() const
{
	return *data_->get();
}

template<class T> const T* CRefcountObject_Uninitialized<T>::operator->() const
{
	return data_->get();
}

template<typename T> class CScopedArray
{
public:
	CScopedArray() : v_() {}
	CScopedArray(T* v) : v_(v) {}
	~CScopedArray() { delete [] v_; }

private:
	T* v_;
};

#endif //__REFCOUNT_H__
