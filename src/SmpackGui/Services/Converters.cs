using System.Globalization;
using System.Windows;
using System.Windows.Data;
using SmpackGui.Core.Cli;

namespace SmpackGui.Services;

/// <summary>'true' means visible. Parameter 'invert' flips it.</summary>
public sealed class BoolToVisibility : IValueConverter
{
    public object Convert(object? value, Type target_type, object? parameter, CultureInfo culture)
    {
        bool b = value is bool v && v;

        if (parameter as string == "invert")
            b = !b;

        return b ? Visibility.Visible : Visibility.Collapsed;
    }

    public object ConvertBack(object? value, Type target_type, object? parameter, CultureInfo culture) =>
        value is Visibility v && (v == Visibility.Visible) ^ (parameter as string == "invert");
}

/// <summary>Non-null, non-empty, non-zero mean visible. Parameter 'invert' flips it.</summary>
public sealed class NotNullToVisibility : IValueConverter
{
    public object Convert(object? value, Type target_type, object? parameter, CultureInfo culture)
    {
        bool has = value switch
        {
            null => false,
            string s => s.Length > 0,
            int i => i != 0,
            System.Collections.ICollection c => c.Count > 0,
            _ => true,
        };

        if (parameter as string == "invert")
            has = !has;

        return has ? Visibility.Visible : Visibility.Collapsed;
    }

    public object ConvertBack(object? value, Type target_type, object? parameter, CultureInfo culture) => Binding.DoNothing;
}

public sealed class InverseBool : IValueConverter
{
    public object Convert(object? value, Type target_type, object? parameter, CultureInfo culture) => !(value is bool b && b);
    public object ConvertBack(object? value, Type target_type, object? parameter, CultureInfo culture) => !(value is bool b && b);
}

/// <summary>Enum conversion for radio button groups.</summary>
public sealed class EnumEquals : IValueConverter
{
    public object Convert(object? value, Type target_type, object? parameter, CultureInfo culture) =>
        value != null && parameter != null && value.ToString() == parameter.ToString();

    public object ConvertBack(object? value, Type target_type, object? parameter, CultureInfo culture) =>
        value is true && parameter != null ? Enum.Parse(target_type, parameter.ToString()!) : Binding.DoNothing;
}

public sealed class BytesToText : IValueConverter
{
    public object Convert(object? value, Type target_type, object? parameter, CultureInfo culture) =>
        value is long l ? ImageUtil.HumanBytes(l) : value is int i ? ImageUtil.HumanBytes(i) : "";
    public object ConvertBack(object? value, Type target_type, object? parameter, CultureInfo culture) => Binding.DoNothing;
}

public sealed class LogKindToBrush : IValueConverter
{
    public object Convert(object? value, Type target_type, object? parameter, CultureInfo culture)
    {
        var key = value is LogKind k ? k switch
        {
            LogKind.COMMAND => "Accent",
            LogKind.ERROR => "Danger",
            LogKind.SUCCESS => "Success",
            LogKind.WARNING => "Warning",
            LogKind.INFO => "Text",
            _ => "TextDim",
        } : "TextDim";

        return Application.Current.FindResource(key);
    }

    public object ConvertBack(object? value, Type target_type, object? parameter, CultureInfo culture) => Binding.DoNothing;
}