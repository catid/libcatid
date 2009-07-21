/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

// 06/11/09 part of libcat-1.0

#ifndef CAT_CAT_SINGLETON_HPP
#define CAT_CAT_SINGLETON_HPP

#include <cat/Platform.hpp>

namespace cat {


#define CAT_SINGLETON(subclass) \
    private: \
        friend class Singleton<subclass>; \
        subclass()


template<class T> class Singleton
{
protected:
    Singleton<T>() {}
    Singleton<T>(Singleton<T> &) {}
    Singleton<T> &operator=(Singleton<T> &) {}

public:
    static T *ii;

public:
    inline static T *ref()
    {
        if (ii) return ii;
        return ii = new T;
    }
};

template<class T> T *Singleton<T>::ii = 0;


} // namespace cat

#endif // CAT_CAT_SINGLETON_HPP
