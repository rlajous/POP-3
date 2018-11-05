cmake_minimum_required(VERSION 2.8)

add_executable(stripmime
            mime_body.c
            mime_boundary_border_end.c
            mime_boundary_key.c
            mime_chars.c
            mime_ctransfer_value.c
            mime_msg.c
            mime_value.c
            parser.c
            parser_utils.c
            pop3_multi.c
            stripmime.c
        )
