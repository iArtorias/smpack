using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Input;

namespace SmpackGui.Mvvm;

public abstract class ObservableObject : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
            return false;

        field = value;

        OnPropertyChanged(name);
        return true;
    }
}

public sealed class RelayCommand(Action<object?> execute, Func<object?, bool>? can_execute = null) : ICommand
{
    public RelayCommand(Action execute, Func<bool>? can_execute = null)
        : this(_ => execute(), can_execute == null ? null : _ => can_execute()) { }

    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }

    public bool CanExecute(object? parameter) => can_execute?.Invoke(parameter) ?? true;
    public void Execute(object? parameter) => execute(parameter);
}

/// <summary>Async command that disables itself while running and routes errors to a handler.</summary>
public sealed class AsyncCommand(Func<object?, Task> execute, Func<object?, bool>? can_execute = null) : ICommand
{
    private bool m_Running;

    public AsyncCommand(Func<Task> execute, Func<bool>? can_execute = null)
        : this(_ => execute(), can_execute == null ? null : _ => can_execute()) { }

    public static Action<Exception>? ErrorHandler
    {
        get;
        set;
    }

    public event EventHandler? CanExecuteChanged
    {
        add => CommandManager.RequerySuggested += value;
        remove => CommandManager.RequerySuggested -= value;
    }

    public bool CanExecute(object? parameter) => !m_Running && (can_execute?.Invoke(parameter) ?? true);

    public async void Execute(object? parameter)
    {
        m_Running = true;
        CommandManager.InvalidateRequerySuggested();

        try
        {
            await execute(parameter);
        }
        catch (OperationCanceledException) 
        {}
        catch (Exception ex)
        { 
            ErrorHandler?.Invoke(ex);
        }
        finally
        {
            m_Running = false;
            CommandManager.InvalidateRequerySuggested();
        }
    }
}