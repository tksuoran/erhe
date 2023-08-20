#include "erhe_net/net_os.hpp"
#include "erhe_net/net_log.hpp"
#include <string.h>

#include <fmt/format.h>
 
namespace erhe::net
{

auto initialize_net() -> bool
{
    return true;
}

auto get_net_last_error() -> int
{
    return errno;
}

auto get_net_error_code_string(const int error_code) -> std::string
{
    switch (error_code) {
        case EPERM          : return "EPERM";
        case ENOENT         : return "ENOENT";
        case ESRCH          : return "ESRCH";
        case EINTR          : return "EINTR";
        case EIO            : return "EIO";
        case ENXIO          : return "ENXIO";
        case E2BIG          : return "E2BIG";
        case ENOEXEC        : return "ENOEXEC";
        case EBADF          : return "EBADF";
        case ECHILD         : return "ECHILD";
        case EAGAIN         : return "EAGAIN";
        case ENOMEM         : return "ENOMEM";
        case EACCES         : return "EACCES";
        case EFAULT         : return "EFAULT";
        case ENOTBLK        : return "ENOTBLK";
        case EBUSY          : return "EBUSY";
        case EEXIST         : return "EEXIST";
        case EXDEV          : return "EXDEV";
        case ENODEV         : return "ENODEV";
        case ENOTDIR        : return "ENOTDIR";
        case EISDIR         : return "EISDIR";
        case EINVAL         : return "EINVAL";
        case ENFILE         : return "ENFILE";
        case EMFILE         : return "EMFILE";
        case ENOTTY         : return "ENOTTY";
        case ETXTBSY        : return "ETXTBSY";
        case EFBIG          : return "EFBIG";
        case ENOSPC         : return "ENOSPC";
        case ESPIPE         : return "ESPIPE";
        case EROFS          : return "EROFS";
        case EMLINK         : return "EMLINK";
        case EPIPE          : return "EPIPE";
        case EDOM           : return "EDOM";
        case ERANGE         : return "ERANGE";
        case EDEADLK        : return "EDEADLK";
        case ENAMETOOLONG   : return "ENAMETOOLONG";
        case ENOLCK         : return "ENOLCK";
        case ENOSYS         : return "ENOSYS";
        case ENOTEMPTY      : return "ENOTEMPTY";
        case ELOOP          : return "ELOOP";
        //case EWOULDBLOCK    : return "EWOULDBLOCK";
        case ENOMSG         : return "ENOMSG";
        case EIDRM          : return "EIDRM";
        case ECHRNG         : return "ECHRNG";
        case EL2NSYNC       : return "EL2NSYNC";
        case EL3HLT         : return "EL3HLT";
        case EL3RST         : return "EL3RST";
        case ELNRNG         : return "ELNRNG";
        case EUNATCH        : return "EUNATCH";
        case ENOCSI         : return "ENOCSI";
        case EL2HLT         : return "EL2HLT";
        case EBADE          : return "EBADE";
        case EBADR          : return "EBADR";
        case EXFULL         : return "EXFULL";
        case ENOANO         : return "ENOANO";
        case EBADRQC        : return "EBADRQC";
        case EBADSLT        : return "EBADSLT";
        case EBFONT         : return "EBFONT";
        case ENOSTR         : return "ENOSTR";
        case ENODATA        : return "ENODATA";
        case ETIME          : return "ETIME";
        case ENOSR          : return "ENOSR";
        case ENONET         : return "ENONET";
        case ENOPKG         : return "ENOPKG";
        case EREMOTE        : return "EREMOTE";
        case ENOLINK        : return "ENOLINK";
        case ESRMNT         : return "ESRMNT";
        case ECOMM          : return "ECOMM";
        case EPROTO         : return "EPROTO";
        case EMULTIHOP      : return "EMULTIHOP";
        case EDOTDOT        : return "EDOTDOT";
        case EBADMSG        : return "EBADMSG";
        case EOVERFLOW      : return "EOVERFLOW";
        case ENOTUNIQ       : return "ENOTUNIQ";
        case EBADFD         : return "EBADFD";
        case EREMCHG        : return "EREMCHG";
        case ELIBACC        : return "ELIBACC";
        case ELIBBAD        : return "ELIBBAD";
        case ELIBSCN        : return "ELIBSCN";
        case ELIBMAX        : return "ELIBMAX";
        case ELIBEXEC       : return "ELIBEXEC";
        case EILSEQ         : return "EILSEQ";
        case ERESTART       : return "ERESTART";
        case ESTRPIPE       : return "ESTRPIPE";
        case EUSERS         : return "EUSERS";
        case ENOTSOCK       : return "ENOTSOCK";
        case EDESTADDRREQ   : return "EDESTADDRREQ";
        case EMSGSIZE       : return "EMSGSIZE";
        case EPROTOTYPE     : return "EPROTOTYPE";
        case ENOPROTOOPT    : return "ENOPROTOOPT";
        case EPROTONOSUPPORT: return "EPROTONOSUPPORT";
        case ESOCKTNOSUPPORT: return "ESOCKTNOSUPPORT";
        case EOPNOTSUPP     : return "EOPNOTSUPP";
        case EPFNOSUPPORT   : return "EPFNOSUPPORT";
        case EAFNOSUPPORT   : return "EAFNOSUPPORT";
        case EADDRINUSE     : return "EADDRINUSE";
        case EADDRNOTAVAIL  : return "EADDRNOTAVAIL";
        case ENETDOWN       : return "ENETDOWN";
        case ENETUNREACH    : return "ENETUNREACH";
        case ENETRESET      : return "ENETRESET";
        case ECONNABORTED   : return "ECONNABORTED";
        case ECONNRESET     : return "ECONNRESET";
        case ENOBUFS        : return "ENOBUFS";
        case EISCONN        : return "EISCONN";
        case ENOTCONN       : return "ENOTCONN";
        case ESHUTDOWN      : return "ESHUTDOWN";
        case ETOOMANYREFS   : return "ETOOMANYREFS";
        case ETIMEDOUT      : return "ETIMEDOUT";
        case ECONNREFUSED   : return "ECONNREFUSED";
        case EHOSTDOWN      : return "EHOSTDOWN";
        case EHOSTUNREACH   : return "EHOSTUNREACH";
        case EALREADY       : return "EALREADY";
        case EINPROGRESS    : return "EINPROGRESS";
        case ESTALE         : return "ESTALE";
        case EUCLEAN        : return "EUCLEAN";
        case ENOTNAM        : return "ENOTNAM";
        case ENAVAIL        : return "ENAVAIL";
        case EISNAM         : return "EISNAM";
        case EREMOTEIO      : return "EREMOTEIO";
        case EDQUOT         : return "EDQUOT";
        case ENOMEDIUM      : return "ENOMEDIUM";
        case EMEDIUMTYPE    : return "EMEDIUMTYPE";
        case ECANCELED      : return "ECANCELED";
        case ENOKEY         : return "ENOKEY";
        case EKEYEXPIRED    : return "EKEYEXPIRED";
        case EKEYREVOKED    : return "EKEYREVOKED";
        case EKEYREJECTED   : return "EKEYREJECTED";
        case EOWNERDEAD     : return "EOWNERDEAD";
        case ENOTRECOVERABLE: return "ENOTRECOVERABLE";
        case ERFKILL        : return "ERFKILL";
        case EHWPOISON      : return "EHWPOISON";
        default:
            return fmt::format("{}", error_code);
    }
}

auto get_net_error_message_string(const int error_code) -> std::string
{
    const char* const description = strerror(error_code);
    return fmt::format("Error {}: {}", error_code, description);
}

auto is_error_fatal(const int error_code) -> bool
{
    if (is_error_busy(error_code)) {
        return false;
    }
    if (error_code == EISCONN) {
        return false;
    }
    return true;
}

auto is_error_busy(const int error_code) -> bool
{
    switch (error_code) {
        case EWOULDBLOCK:
        case EINPROGRESS:
        case EALREADY: {
            return true;
        }
        default: {
            return false;
        }
    }
}

auto is_socket_good(const SOCKET socket) -> bool
{
    return (socket >= 0) && (socket <= FD_SETSIZE);
}

auto set_socket_option(
    const SOCKET        socket,
    const Socket_option option,
    const int           value
) -> bool
{
    if (!is_socket_good(socket)) {
        return false;
    }
    int               result = SOCKET_ERROR;
    const void* const optval = reinterpret_cast<const void*>(&value);
    const int         optlen = sizeof(int);
    switch (option) {
        case Socket_option::Error: {
            return false;
        }
        case Socket_option::NonBlocking: {
            int flags = fcntl(socket, F_GETFL, 0);
            if (flags == -1) {
                return false;
            }
            flags = (value != 0) ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
            result = fcntl(socket, F_SETFL, flags);
            break;
        }
        case Socket_option::ReceiveBufferSize: {
            result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, optval, optlen);
            break;
        }
        case Socket_option::SendBufferSize: {
            result = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, optval, optlen);
            break;
        }
        case Socket_option::ReuseAddress: {
            result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, optval, optlen);
            break;
        }
        case Socket_option::ReceiveTimeout: {
            result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, optval, optlen);
            break;
        }
        case Socket_option::SendTimeout: {
            result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, optval, optlen);
            break;
        }
        case Socket_option::NoDelay: {
            result = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, optval, optlen);
            break;
        }
        default: break;
    }
    if (result == SOCKET_ERROR) {
        log_net->error(
            "Setting socket option {} to value {} failed with error {}",
            c_str(option),
            value,
            get_net_last_error_message()
        );
        return false;
    }
    return true;
}

auto get_socket_option(
    const SOCKET        socket,
    const Socket_option option
) -> std::optional<int>
{
    int         result = SOCKET_ERROR;
    int         value  = 0;
    void* const optval = reinterpret_cast<void*>(&value);
    socklen_t   optlen = sizeof(int);
    switch (option) {
        case Socket_option::Error: {
            result = getsockopt(socket, SOL_SOCKET, SO_ERROR, optval, &optlen);
            break;
        }
        case Socket_option::ReceiveBufferSize: {
            result = getsockopt(socket, SOL_SOCKET, SO_RCVBUF, optval, &optlen);
            break;
        }
        case Socket_option::SendBufferSize: {
            result = getsockopt(socket, SOL_SOCKET, SO_SNDBUF, optval, &optlen);
            break;
        }
        default: break;
    }
    if (result == SOCKET_ERROR) {
        log_net->error(
            "Getting socket option {} failed with error {}",
            c_str(option),
            get_net_last_error_message()
        );
        return {};
    }
    return value;
}

auto get_net_hints(const int flags, const int family, const int socktype, const int protocol) -> addrinfo
{
    static_cast<void>(flags);
    static_cast<void>(family);
    static_cast<void>(socktype);
    static_cast<void>(protocol);
    return addrinfo{
        .ai_flags     = 0,
        .ai_family    = AF_INET,
        .ai_socktype  = SOCK_STREAM,
        .ai_protocol  = IPPROTO_TCP,
        .ai_addrlen   = 0,
        .ai_addr      = nullptr,
        .ai_canonname = nullptr,
        .ai_next      = nullptr
    };
}

}
