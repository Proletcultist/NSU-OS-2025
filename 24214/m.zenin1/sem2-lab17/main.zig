const std = @import("std");
const Mutex = std.Io.Mutex;
const Allocator = std.mem.Allocator;

// Node of linked list
const Node = struct {
    value: []const u8 = undefined,
    next: ?*Node = null,
};

const StringList = struct {
    first: *Node,
    last: *Node,
    len: usize = 0,
    alloc: Allocator,

    fn init(alloc: Allocator) !StringList {
        const sentinel: *Node = try alloc.create(Node);
        sentinel.* = .{};
        return .{.first = sentinel, .last = sentinel, .alloc = alloc};
    }

    fn prepend(self: *StringList, val: []const u8) !void {
        const new: *Node = try self.alloc.create(Node);
        new.* = .{.value = val};
        
        self.last.next = new;
        self.last = new;

        self.len += 1;
    }

    fn print(self: *StringList, w: *std.Io.Writer) !void {
        var current = self.first.next;
        while (current != null) : (current = current.?.next) {
            try w.print("{s}\n", .{current.?.value});
        }
    }

    fn deinit(self: *StringList) void {
        var current: ?*Node = self.first;
        while (current != null) {
            const tmp = current.?.next;
            self.alloc.destroy(current.?);
            current = tmp;
        }
    }
};

// String list
var list: StringList = undefined;
var list_mtx: Mutex = std.Io.Mutex.init;
  
fn sort_list(li: *StringList) void {
    for (0..li.len) |i| {
        var swapped = false;

        var current = li.first;
        for (0..li.len - i - 1) |_| {
            const this = current.next.?;
            const this_next = this.next.?;

            if (std.mem.order(u8, this.value, this_next.value) == std.math.Order.gt) {
                this.next = this_next.next;
                this_next.next = this;
                current.next = this_next;
                if (this.next == null) {
                    li.last = this;
                }

                swapped = true;
            }

            current = current.next.?;
        }

        if (!swapped) {
            break;
        }
    }
}

fn sorter_routine(io: std.Io) void {
    const sleep_req: std.c.timespec = std.c.timespec{ .sec = 5, .nsec = 0};

    while (true) {
        // If we caught cancellation error - stop sorter
        list_mtx.lock(io) catch {
            break;
        };
        sort_list(&list);
        list_mtx.unlock(io);

        // If we caught cancellation error - stop sorter
        if (std.c.nanosleep(&sleep_req, null) == -1) {
            break;
        }
    }
}

pub fn main(init: std.process.Init) !void {
    // Get the Io implementation
    const io = init.io;

    // Init buffered stdin reader
    var stdin_buf: [1024]u8 = undefined;
    var stdin_reader = std.Io.File.stdin().reader(io, &stdin_buf);
    const stdin = &stdin_reader.interface;

    // Init buffered stdout writer
    var stdout_buf: [1024]u8 = undefined;
    var stdout_writer = std.Io.File.stdout().writer(io, &stdout_buf);
    const stdout = &stdout_writer.interface;

    // Init allocator
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    // Init string list
    list = try StringList.init(allocator);
    defer list.deinit();

    // Spawn sorter thread
    _ = try std.Thread.spawn(.{}, sorter_routine, .{io});

    var input_buffer = std.Io.Writer.Allocating.init(allocator);
    defer input_buffer.deinit();

    while (true) {
        if (stdin.streamDelimiterLimit(&input_buffer.writer, '\n', .unlimited)) |readen| {
            list_mtx.lockUncancelable(io);
            defer list_mtx.unlock(io);
            if (readen == 0 and stdin.seek != stdin.end) {
                try list.print(stdout);
                try stdout.flush();
            }
            else {
                try list.prepend(try input_buffer.toOwnedSlice());
            }

            if (stdin.seek == stdin.end) break
            else stdin.toss(1);
        }
        else |err| switch (err) {
            error.StreamTooLong => unreachable,
            else => |e| return e
        }
    }

    var current = list.first.next;
    while (current != null) : (current = current.?.next) {
        input_buffer.allocator.free(current.?.value);
    }
}
