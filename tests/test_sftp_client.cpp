/*
 * Copyright (C) 2019 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "mock_sftp.h"
#include "mock_ssh.h"

#include "file_operations.h"
#include "path.h"
#include "temp_dir.h"

#include <multipass/ssh/sftp_client.h>
#include <multipass/ssh/ssh_session.h>

#include <gmock/gmock.h>


namespace mp = multipass;
namespace mpt = multipass::test;

namespace
{
struct SFTPClient : public testing::Test
{
    SFTPClient()
        : sftp_new{mock_sftp_new,
            [](ssh_session session) -> sftp_session {
                sftp_session sftp;
                sftp = (sftp_session)std::calloc(1, sizeof(struct sftp_session_struct));
                return sftp;
                }}, free_sftp{mock_sftp_free, [](sftp_session sftp) { std::free(sftp); }}
    {
        connect.returnValue(SSH_OK);
        is_connected.returnValue(true);
        open_session.returnValue(SSH_OK);
        close.returnValue(SSH_OK);
    }

    mp::SFTPClient make_sftp_client()
    {
        return {std::make_unique<mp::SSHSession>("b", 43)};
    }

    decltype(MOCK(ssh_connect)) connect{MOCK(ssh_connect)};
    decltype(MOCK(ssh_is_connected)) is_connected{MOCK(ssh_is_connected)};
    decltype(MOCK(ssh_channel_open_session)) open_session{MOCK(ssh_channel_open_session)};
    decltype(MOCK(sftp_close)) close{MOCK(sftp_close)};
    MockScope<decltype(mock_sftp_new)> sftp_new;
    MockScope<decltype(mock_sftp_free)> free_sftp;
};
}

// testing sftp_session

TEST_F(SFTPClient, throws_when_unable_to_allocate_sftp_session)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return nullptr; });

    EXPECT_THROW(sftp.push_file("foo", "bar"), std::runtime_error);
}

// testing push method

TEST_F(SFTPClient, push_throws_when_failed_to_init)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_ERROR; });

    EXPECT_THROW(sftp.push_file("foo", "bar"), std::runtime_error);
}

TEST_F(SFTPClient, push_throws_on_sftp_open_failed)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_OK; });
    REPLACE(sftp_open, [](auto...) { return nullptr; });

    EXPECT_THROW(sftp.push_file("foo", "bar"), std::runtime_error);
}

TEST_F(SFTPClient, push_throws_on_sftp_read_failed)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_OK; });
    REPLACE(sftp_open, [](auto...) { return nullptr; });
    REPLACE(sftp_get_error, [](auto...) { return SSH_OK; });

    EXPECT_THROW(sftp.push_file("foo", "bar"), std::runtime_error);
}

TEST_F(SFTPClient, push_throws_on_sftp_write_error)
{
    mpt::TempDir temp_dir;
    auto file_name = temp_dir.path() + "/test-file";
    mpt::make_file_with_content(file_name);

    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_OK; });
    REPLACE(sftp_open, [](auto...) { return nullptr; });
    REPLACE(sftp_write, [](auto...) { return 0; });
    REPLACE(sftp_get_error, [](auto...)
    {
        static int errorCount = 0;
        return (errorCount-- < 0) ? SSH_ERROR : SSH_OK;
    });


    EXPECT_THROW(sftp.push_file(file_name.toStdString(), "bar"), std::runtime_error);
}

// testing pull method

TEST_F(SFTPClient, pull_throws_when_failed_to_init)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_ERROR; });

    EXPECT_THROW(sftp.pull_file("foo", "bar"), std::runtime_error);
}

TEST_F(SFTPClient, pull_throws_on_sftp_get_stat_failed)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_OK; });
    REPLACE(sftp_stat, [](auto...) { return nullptr; });

    EXPECT_THROW(sftp.pull_file("foo", "bar"), std::runtime_error);
}

TEST_F(SFTPClient, pull_throws_on_sftp_open_failed)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_OK; });
    REPLACE(sftp_stat, [](auto...) { return new sftp_attributes_struct; });
    REPLACE(sftp_open, [](auto...) { return nullptr; });
    REPLACE(sftp_get_error, [](auto...)
    {
        static int errorCount = 0;
        return (errorCount-- < 0) ? SSH_ERROR : SSH_OK;
    });

    EXPECT_THROW(sftp.pull_file("foo", "bar"), std::runtime_error);
}

TEST_F(SFTPClient, pull_throws_on_sftp_read_failed)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_OK; });
    REPLACE(sftp_stat, [](auto...) { return new sftp_attributes_struct; });
    REPLACE(sftp_open, [](auto...) { return new sftp_file_struct; });
    REPLACE(sftp_read, [](auto...) { return 0; });
    REPLACE(sftp_get_error, [](auto...)
    {
        static int errorCount = 1;
        return (errorCount-- < 0) ? SSH_ERROR : SSH_OK;
    });

    EXPECT_THROW(sftp.pull_file("foo", "bar"), std::runtime_error);
}

// testing stream method

TEST_F(SFTPClient, stream_throws_when_failed_to_init)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_ERROR; });

    EXPECT_THROW(sftp.stream_file("bar"), std::runtime_error);
}

TEST_F(SFTPClient, steam_throws_on_sftp_open_failed)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_OK; });
    REPLACE(sftp_open, [](auto...) { return nullptr; });
    REPLACE(sftp_get_error, [](auto...) { return SSH_ERROR; });

    EXPECT_THROW(sftp.stream_file("bar"), std::runtime_error);
}

TEST_F(SFTPClient, stream_throws_on_write_failed)
{
    auto sftp = make_sftp_client();

    REPLACE(sftp_new, [](auto...) { return new sftp_session_struct; });
    REPLACE(sftp_init, [](auto...) { return SSH_OK; });
    REPLACE(sftp_open, [](auto...) { return nullptr; });
    REPLACE(sftp_write, [](auto...) { return 0; });
    REPLACE(sftp_get_error, [](auto...)
    {
        static int errorCount = 0;
        return (errorCount-- < 0) ? SSH_ERROR : SSH_OK;
    });

    mpt::TempDir temp_dir;
    auto file_name = temp_dir.path() + "/test-file";
    mpt::make_file_with_content(file_name, "testing stream :-)");

    // NOTE: remember that we received up to 10 bytes per read
    freopen(file_name.toStdString().c_str(),"r",stdin);

    EXPECT_THROW(sftp.stream_file("bar"), std::runtime_error);
}
