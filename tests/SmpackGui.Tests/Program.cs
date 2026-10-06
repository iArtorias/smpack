using SmpackGui.Core.Imaging;

// Usage: 'decode <dir-with-dds> <outdir>'.
// Writes '<name>.rgba [mip0, slice0]' for comparison.
if (args.Length >= 3 && args[0] == "decode")
{
    Directory.CreateDirectory(args[2]);

    int ok = 0, fail = 0;
    var sw = System.Diagnostics.Stopwatch.StartNew();

    foreach (var f in Directory.EnumerateFiles(args[1], "*.dds", SearchOption.AllDirectories))
    {
        try
        {
            var dds = DdsFile.Load(f);
            var img = TextureDecoder.Decode(dds, 0, 0);

            File.WriteAllBytes(Path.Combine(args[2], Path.GetFileNameWithoutExtension(f) + $".{img.Width}x{img.Height}.{dds.Dxgi}.rgba"), img.Rgba);

            if (img.Hdr != null)
            {
                var b = new byte[img.Hdr.Length * 4];

                Buffer.BlockCopy(img.Hdr, 0, b, 0, b.Length);
                File.WriteAllBytes(Path.Combine(args[2], Path.GetFileNameWithoutExtension(f) + $".{img.Width}x{img.Height}.{dds.Dxgi}.f32"), b);
            }

            ok++;
        }
        catch (Exception e)
        {
            Console.WriteLine($"FAIL {f}: {e.Message}");
            fail++;
        }
    }

    Console.WriteLine($"decoded {ok}, failed {fail} in {sw.ElapsedMilliseconds} ms");
    return fail == 0 ? 0 : 1;
}

if (args.Length >= 2 && args[0] == "volume")
{
    var d = DdsFile.Load(args[1]);
    Console.WriteLine($"{d.Width}x{d.Height}x{d.Depth} volume={d.IsVolume} {d.FormatName}");

    for (int z = 0; z < d.Depth; z += Math.Max(1, d.Depth / 4))
    {
        var img = TextureDecoder.Decode(d, 0, 0, 1f, z);
        long sum = 0;

        foreach (var b in img.Rgba)
            sum += b;

        Console.WriteLine($"  layer {z}: {img.Width}x{img.Height} checksum {sum}");
    }

    return 0;
}

if (args.Length >= 7 && args[0] == "media")
    return await SmpackGui.Tests.MediaTests.RunAsync(args);

if (args.Length >= 4 && args[0] == "models")
    return await SmpackGui.Tests.MediaTests.ModelsOnlyAsync(args);

return await SmpackGui.Tests.UnitTests.RunAsync(args);